// Dev-only console command: headless-with-rendering check of the RAIN VISUALS of a temperate level (environment section 18, UE-only;
// docs/systems/environment-weather.md section 18). Needs a renderer (the rain FX never spawns without one): test_map.json runs it
// "rendered" (-RenderOffscreen) on /Game/Environment/Weather/Test/L_WeatherRainTest:
//   Scripts/smoke_parallel.ps1 -Commands RainSmoke
// Stages (the dev run starts neutral = SunnyDay 15:00, the clock frozen here):
//   1. SunnyDay: the presenter created its rain component (temperate), nothing falls, no screen drops, no puddle splashes, the object /
//      ground overlay is in WET mode and shows nothing on the dry level;
//   2. Rain (soaked ground, puddles 0.6): the rain system active at DA_RainFX.RainLookIntensity, particles in the air, the screen drops on
//      (strength = DA x MPC ScreenDrops x RainIntensity), puddle splashes active (rate = DA x intensity x puddles), MPC Wetness /
//      PuddleAmount = the state, the wet overlays on the props and the floor;
//   3. Storm: the look at StormLookIntensity, spawn rate x2+ and more particles than in rain, the screen drops stronger; lightning OFF
//      under the smoke (automated run) until Codex.Env.Lightning 1, then a strike flashes (deterministic schedule from the env seed);
//   4. the leader inside the hut: a rain kill box contains him (no rain under the roof - the falling snow's indoor rule);
//   5. SunnyDay again: the rain fades out and deactivates, no screen drops / puddle splashes;
//   6. Codex.Env.Climate arctic: the rain stops at once, the overlay leaves the wet mode (and the arctic presenter never had rain).
// Prints "[RainSmoke] PASS/FAIL ..." and "Smoke RESULT: PASS|FAIL", then exits.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Environment/EnvironmentRules.h"
#include "Environment/EnvironmentSubsystem.h"
#include "Environment/WeatherPresenterActor.h"
#include "Environment/WeatherRainFxComponent.h"
#include "Environment/WeatherSnowOverlayComponent.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"

namespace RainSmoke
{
	constexpr float StepSeconds = 0.05f;
	constexpr float ApplyTimeoutSeconds = 15.f;
	/** Game seconds to let the look settle after a weather change (rise 0.4 / s, fall 0.3 / s, drops fill the air in ~2.5 s). */
	constexpr float SettleSeconds = 4.f;
	constexpr float FadeOutSeconds = 6.f;
	/** The hut of L_WeatherRainTest (create_weather_rain_test_map.py): centre (+900, +300), floor at z 0. */
	const FVector HutSpot(900., 300., 95.);

	enum class EStage : uint8 { WaitApplying, Rain, Storm, StormLightning, Hut, Sunny, Arctic };

	struct FState
	{
		EStage Stage = EStage::WaitApplying;
		float RunStart = -1.f;
		float StageStart = 0.f;
		float StageTime = 0.f;
		float TotalTime = 0.f;
		int32 Failures = 0;
		int32 RainParticles = 0;
		float RainSpawnRate = 0.f;
		float RainScreenDrops = 0.f;
		int32 StrikesBefore = 0;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[RainSmoke] %s: %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	void Finish(FState& State)
	{
		if (IConsoleVariable* Lightning = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Env.Lightning")))
		{
			Lightning->Set(TEXT("-1"), ECVF_SetByConsole);
		}
		const bool bPass = State.Failures == 0;
		UE_LOG(LogCodexTactics, Display, TEXT("[RainSmoke] RESULT %s (%d failed checks)"), bPass ? TEXT("PASS") : TEXT("FAIL"), State.Failures);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), bPass ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("RainSmoke"));
	}

	void NextStage(FState& State, EStage Stage)
	{
		State.Stage = Stage;
		State.StageStart += State.StageTime;
		State.StageTime = 0.f;
	}

	void Exec(UWorld& World, const TCHAR* Command)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[RainSmoke] > %s"), Command);
		GEngine->Exec(&World, Command);
	}

	float Mpc(AWeatherPresenterActor& Presenter, const TCHAR* Name)
	{
		float Value = -1.f;
		return Presenter.GetEnvironmentScalar(FName(Name), Value) ? Value : -1.f;
	}

	/** Weather + wetness / puddles set at once (a state jump: the presenter snaps), the clock frozen. */
	void SetWeather(UEnvironmentSubsystem& Env, EWeatherState Weather, float Wetness, float Puddles)
	{
		FEnvironmentState Next = Env.GetState();
		Next.Weather = Weather;
		Next.NextWeather = Weather;
		Next.WeatherElapsedSeconds = 0.f;
		Next.Wetness = Wetness;
		Next.PuddleAmount = Puddles;
		Env.SetState(Next);
		Env.SetClockScale(0.f);
	}

	void CheckSunnyStart(FState& State, UEnvironmentSubsystem& Env, AWeatherPresenterActor& Presenter)
	{
		UWeatherRainFxComponent* Rain = Presenter.GetRainFx();
		UE_LOG(LogCodexTactics, Display, TEXT("[RainSmoke] start: %s; rain %s"), *Env.Describe(), Rain ? *Rain->DescribeParticles() : TEXT("none"));
		Check(State, Env.GetClimate() == EClimateProfile::Temperate && Env.GetWeatherDisplayName() == TEXT("SunnyDay"),
			FString::Printf(TEXT("1 temperate level, SunnyDay start (%s)"), *Env.GetWeatherDisplayName()));
		Check(State, Rain != nullptr, TEXT("1 the presenter created its rain component on the temperate level"));
		if (!Rain)
		{
			return;
		}
		Check(State, Rain->IsUsingSettingsAsset(), TEXT("1 the look comes from DA_RainFX"));
		Check(State, !Rain->IsRainActive() && Rain->GetTargetIntensity() == 0.f && Rain->GetParticleCount() == 0,
			FString::Printf(TEXT("1 SunnyDay: no rain (target %.2f, %d particles)"), Rain->GetTargetIntensity(), Rain->GetParticleCount()));
		Check(State, !Rain->AreScreenDropsOn() && Rain->GetLastScreenDropsStrength() == 0.f, TEXT("1 SunnyDay: no screen drops"));
		Check(State, !Rain->ArePuddleSplashesActive(), TEXT("1 SunnyDay: no puddle splashes"));
		const UWeatherSnowOverlayComponent* Overlay = Presenter.GetSnowOverlay();
		Check(State, Overlay && Overlay->IsWetMode() && Overlay->GetObjectTargetCount() == 0 && Overlay->GetGroundTargetCount() == 0,
			FString::Printf(TEXT("1 SunnyDay (dry): the overlay is in wet mode and shows nothing (%d objects, %d slabs)"),
				Overlay ? Overlay->GetObjectTargetCount() : -1, Overlay ? Overlay->GetGroundTargetCount() : -1));
	}

	void CheckRain(FState& State, UEnvironmentSubsystem& Env, AWeatherPresenterActor& Presenter, UWeatherRainFxComponent& Rain)
	{
		const FWeatherRainFxTuning& Look = Rain.GetActiveTuning();
		UE_LOG(LogCodexTactics, Display, TEXT("[RainSmoke] rain: %s; %s"), *Env.Describe(), *Rain.DescribeParticles());
		Check(State, Rain.IsRainActive(), TEXT("2 Rain: the rain system is active"));
		Check(State, FMath::IsNearlyEqual(Rain.GetTargetIntensity(), Look.RainLookIntensity, 1.e-3f)
			&& FMath::IsNearlyEqual(Rain.GetCurrentIntensity(), Look.RainLookIntensity, 1.e-3f),
			FString::Printf(TEXT("2 Rain: look intensity %.2f = DA_RainFX.RainLookIntensity %.2f"), Rain.GetCurrentIntensity(), Look.RainLookIntensity));
		State.RainParticles = Rain.GetParticleCount();
		State.RainSpawnRate = Rain.GetLastParams().SpawnRate;
		Check(State, State.RainParticles > 1000, FString::Printf(TEXT("2 Rain: drops in the air (%d)"), State.RainParticles));
		const float RainIntensity = Env.GetRainIntensity();
		const float ExpectedDrops = WeatherRainFxRules::ComputeScreenDropsStrength(1.f, RainIntensity, Look);
		State.RainScreenDrops = Rain.GetLastScreenDropsStrength();
		Check(State, Rain.AreScreenDropsOn() && ExpectedDrops > 0.f && FMath::IsNearlyEqual(State.RainScreenDrops, ExpectedDrops, 1.e-4f),
			FString::Printf(TEXT("2 Rain: screen drops on, strength %.3f = %.2f x ScreenDrops 1 x rain %.2f"), State.RainScreenDrops, Look.ScreenDropsStrength, RainIntensity));
		Check(State, FMath::IsNearlyEqual(Mpc(Presenter, TEXT("ScreenDrops")), 1.f) && FMath::IsNearlyEqual(Mpc(Presenter, TEXT("Wetness")), Env.GetWetness(), 1.e-3f)
			&& FMath::IsNearlyEqual(Mpc(Presenter, TEXT("PuddleAmount")), Env.GetPuddleAmount(), 1.e-3f),
			FString::Printf(TEXT("2 Rain: MPC ScreenDrops 1, Wetness %.2f, PuddleAmount %.2f = the state"), Mpc(Presenter, TEXT("Wetness")), Mpc(Presenter, TEXT("PuddleAmount"))));
		const float ExpectedSplashes = WeatherRainFxRules::ComputePuddleSplashRate(Rain.GetCurrentIntensity(), Env.GetPuddleAmount(), Look);
		Check(State, Rain.ArePuddleSplashesActive() && ExpectedSplashes > 0.f && FMath::IsNearlyEqual(Rain.GetLastPuddleSplashRate(), ExpectedSplashes, 0.01f),
			FString::Printf(TEXT("2 Rain: puddle splashes %.0f / s = intensity x puddles x %.0f"), Rain.GetLastPuddleSplashRate(), Look.PuddleSplashRateAtFull));
		const UWeatherSnowOverlayComponent* Overlay = Presenter.GetSnowOverlay();
		Check(State, Overlay && Overlay->IsWetMode() && Overlay->GetObjectTargetCount() > 0 && Overlay->GetGroundTargetCount() > 0,
			FString::Printf(TEXT("2 Rain: the wet overlays are on (%d objects, %d ground slabs)"), Overlay ? Overlay->GetObjectTargetCount() : -1,
				Overlay ? Overlay->GetGroundTargetCount() : -1));
		Check(State, Env.GetSnowAmount() == 0.f && Mpc(Presenter, TEXT("IsSnowing")) == 0.f, TEXT("2 Rain: no snow"));
	}

	void CheckStorm(FState& State, UEnvironmentSubsystem& Env, UWeatherRainFxComponent& Rain)
	{
		const FWeatherRainFxTuning& Look = Rain.GetActiveTuning();
		const int32 Particles = Rain.GetParticleCount();
		UE_LOG(LogCodexTactics, Display, TEXT("[RainSmoke] storm: %s; %s"), *Env.Describe(), *Rain.DescribeParticles());
		Check(State, Rain.IsRainActive() && FMath::IsNearlyEqual(Rain.GetCurrentIntensity(), Look.StormLookIntensity, 1.e-3f),
			FString::Printf(TEXT("2 Storm: look intensity %.2f = StormLookIntensity"), Rain.GetCurrentIntensity()));
		Check(State, Rain.GetLastParams().SpawnRate >= 2.f * State.RainSpawnRate,
			FString::Printf(TEXT("3 Storm: spawn rate %.0f >= 2 x rain %.0f"), Rain.GetLastParams().SpawnRate, State.RainSpawnRate));
		Check(State, Particles > State.RainParticles * 3 / 2, FString::Printf(TEXT("3 Storm: more drops in the air (%d vs rain %d)"), Particles, State.RainParticles));
		Check(State, Rain.GetLastScreenDropsStrength() > State.RainScreenDrops,
			FString::Printf(TEXT("3 Storm: stronger screen drops (%.3f > %.3f)"), Rain.GetLastScreenDropsStrength(), State.RainScreenDrops));
		Check(State, Rain.GetLastParams().DropWindSpeed > 0.f, FString::Printf(TEXT("3 Storm: slanted by the wind (%.0f cm/s)"), Rain.GetLastParams().DropWindSpeed));
		Check(State, !Rain.IsLightningRunning() && Rain.GetStrikeCount() == 0, TEXT("3 Storm: no lightning under a smoke (automated run, Codex.Env.Lightning -1)"));
		State.StrikesBefore = Rain.GetStrikeCount();
		Exec(*Env.GetWorld(), TEXT("Codex.Env.Lightning 1"));
	}

	void CheckLightning(FState& State, UWeatherRainFxComponent& Rain)
	{
		Check(State, Rain.IsLightningRunning(), TEXT("3 Storm + Codex.Env.Lightning 1: the lightning schedule runs"));
		const FWeatherRainFxTuning& Look = Rain.GetActiveTuning();
		Check(State, FMath::IsNearlyEqual(WeatherRainFxRules::ComputeStrikeDelay(7, 3, Look), WeatherRainFxRules::ComputeStrikeDelay(7, 3, Look))
			&& WeatherRainFxRules::ComputeStrikeDelay(7, 3, Look) >= Look.LightningIntervalMin, TEXT("3 the strike schedule is deterministic"));
	}

	bool MoveLeaderIntoHut(UWorld& World)
	{
		USquadSubsystem* Squad = World.GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		return Leader && Leader->SetActorLocation(HutSpot, false, nullptr, ETeleportType::TeleportPhysics);
	}

	void CheckHut(FState& State, UWorld& World, UWeatherRainFxComponent& Rain)
	{
		USquadSubsystem* Squad = World.GetSubsystem<USquadSubsystem>();
		const AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		const FVector Point = Leader ? Leader->GetActorLocation() : HutSpot;
		bool bInside = false;
		for (const FBox& Box : Rain.GetKillBoxes())
		{
			bInside |= WeatherSnowFxRules::IsInsideKillBox(Point, Box);
		}
		Check(State, bInside, FString::Printf(TEXT("4 the leader in the hut (%s) is inside a rain kill box (%d boxes): no rain under the roof"),
			*Point.ToCompactString(), Rain.GetKillBoxes().Num()));
	}

	void CheckSunny(FState& State, UWeatherRainFxComponent& Rain)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[RainSmoke] sunny again: %s"), *Rain.DescribeParticles());
		Check(State, Rain.GetCurrentIntensity() == 0.f && !Rain.IsRainActive(),
			FString::Printf(TEXT("5 SunnyDay: the rain faded out and stopped (intensity %.2f)"), Rain.GetCurrentIntensity()));
		Check(State, !Rain.AreScreenDropsOn() && !Rain.ArePuddleSplashesActive(), TEXT("5 SunnyDay: no screen drops, no puddle splashes"));
	}

	void CheckArctic(FState& State, UEnvironmentSubsystem& Env, AWeatherPresenterActor& Presenter, UWeatherRainFxComponent& Rain)
	{
		Check(State, Env.GetClimate() == EClimateProfile::Arctic, TEXT("6 Codex.Env.Climate arctic"));
		Check(State, Rain.GetTargetIntensity() == 0.f && Rain.GetCurrentIntensity() == 0.f && !Rain.AreScreenDropsOn(),
			TEXT("6 arctic: the rain stops at once, no screen drops"));
		const UWeatherSnowOverlayComponent* Overlay = Presenter.GetSnowOverlay();
		Check(State, Overlay && !Overlay->IsWetMode(), TEXT("6 arctic: the overlay is back in snow mode"));
		Check(State, Mpc(Presenter, TEXT("Wetness")) == 0.f && Mpc(Presenter, TEXT("ScreenDrops")) == 0.f, TEXT("6 arctic: MPC Wetness / ScreenDrops 0"));
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		const float Now = World->GetTimeSeconds();
		if (State.RunStart < 0.f)
		{
			State.RunStart = Now;
			State.StageStart = Now;
		}
		State.StageTime = Now - State.StageStart;
		State.TotalTime = Now - State.RunStart;
		UEnvironmentSubsystem* Env = UEnvironmentSubsystem::Get(World);
		AWeatherPresenterActor* Presenter = AWeatherPresenterActor::Find(World);
		UWeatherRainFxComponent* Rain = Presenter ? Presenter->GetRainFx() : nullptr;
		if (!Env)
		{
			Check(State, false, TEXT("environment subsystem present"));
			Finish(State);
			return false;
		}
		if (State.Stage != EStage::WaitApplying && !Rain)
		{
			Finish(State);
			return false;
		}
		switch (State.Stage)
		{
		case EStage::WaitApplying:
			if (!Presenter || !Presenter->IsApplying() || Presenter->GetApplyCount() == 0 || State.TotalTime < 2.f)
			{
				if (State.TotalTime > ApplyTimeoutSeconds)
				{
					Check(State, false, FString::Printf(TEXT("presenter applying within %.0f game s"), ApplyTimeoutSeconds));
					Finish(State);
					return false;
				}
				return true;
			}
			Check(State, FApp::CanEverRender(), TEXT("0 rendering (the smoke must run rendered)"));
			CheckSunnyStart(State, *Env, *Presenter);
			if (!Presenter->GetRainFx())
			{
				Finish(State);
				return false;
			}
			SetWeather(*Env, EWeatherState::Snowfall, 1.f, 0.6f);
			NextStage(State, EStage::Rain);
			return true;
		case EStage::Rain:
			if (State.StageTime >= SettleSeconds)
			{
				CheckRain(State, *Env, *Presenter, *Rain);
				SetWeather(*Env, EWeatherState::Blizzard, 1.f, 1.f);
				NextStage(State, EStage::Storm);
			}
			return true;
		case EStage::Storm:
			if (State.StageTime >= SettleSeconds)
			{
				CheckStorm(State, *Env, *Rain);
				NextStage(State, EStage::StormLightning);
			}
			return true;
		case EStage::StormLightning:
			if (State.StageTime >= 0.5f)
			{
				CheckLightning(State, *Rain);
				Rain->ForceStrike();
				Check(State, Rain->GetStrikeCount() == State.StrikesBefore + 1, TEXT("3 a forced strike counts"));
				MoveLeaderIntoHut(*World);
				NextStage(State, EStage::Hut);
			}
			return true;
		case EStage::Hut:
			if (State.StageTime >= 0.1f && State.StageTime < 0.2f)
			{
				Check(State, Rain->GetFlashScale() > 0.f, FString::Printf(TEXT("3 the strike flashes (scale %.2f)"), Rain->GetFlashScale()));
			}
			if (State.StageTime >= 1.5f)
			{
				CheckHut(State, *World, *Rain);
				Exec(*World, TEXT("Codex.Env.Lightning -1"));
				SetWeather(*Env, EWeatherState::Clear, 1.f, 1.f);
				NextStage(State, EStage::Sunny);
			}
			return true;
		case EStage::Sunny:
			if (State.StageTime >= FadeOutSeconds)
			{
				CheckSunny(State, *Rain);
				Exec(*World, TEXT("Codex.Env.Climate arctic"));
				NextStage(State, EStage::Arctic);
			}
			return true;
		case EStage::Arctic:
			if (State.StageTime >= 0.5f)
			{
				CheckArctic(State, *Env, *Presenter, *Rain);
				Finish(State);
				return false;
			}
			return true;
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
		TEXT("CodexTactics.RainSmoke"),
		TEXT("Dev check (section 18, rendered, on /Game/Environment/Weather/Test/L_WeatherRainTest): the rain visuals - none in SunnyDay, the rain / storm systems and their density, screen drops, puddle splashes, wet overlays, lightning off under smokes, no rain in the hut, arctic = none; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
