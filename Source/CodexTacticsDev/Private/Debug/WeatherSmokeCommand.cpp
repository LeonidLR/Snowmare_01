// Dev-only console commands for headless checks of the environment VISUALS (AWeatherPresenterActor, environment phase 3, UE-only;
// docs/systems/environment-weather.md §11). Work under -nullrhi (nothing is rendered; the component values are checked):
//   Scripts/smoke.ps1 -Command CodexTactics.WeatherSmoke     -Map /Game/Maps/L_MovementTest
//   Scripts/smoke.ps1 -Command CodexTactics.WeatherLakeSmoke -Map /Game/Maps/L_Demo01_A_FrozenLake -Extra "-LevelJson=L_Demo01_A_FrozenLake.json"
// WeatherSmoke:
//   1. the presenter was spawned (no map placement) and adopted the map's lights (logged: sun, sky atmosphere, sky light, height fog,
//      post-process, moon own / adopted), the volumetric clouds are switched off;
//   2. SetHour 12 -> the sun is above the horizon (rotation pitch < 0), lit, the moon hidden; SetHour 23 -> the sun hidden below the
//      horizon, the moon lit above it;
//   3. ForceWeather Clear / Blizzard (instant) -> the fog density follows the presets, Blizzard > Clear, volumetric fog on in Blizzard;
//   4. Codex.Env.Visuals 0 -> every authored value is back (sun, moon, fog, post-process off, clouds as authored); 1 -> applied again.
// WeatherLakeSmoke: the Demo 01 lake locked at 23:00 Snowfall -> night (sun hidden, moon lit, full night grade) + the Snowfall fog.
// Both print "[WeatherSmoke] PASS/FAIL ..." and "Smoke RESULT: PASS|FAIL", then exit.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "CodexTactics.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Containers/Ticker.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Environment/EnvironmentRules.h"
#include "Environment/EnvironmentSubsystem.h"
#include "Environment/WeatherPresenterActor.h"
#include "HAL/IConsoleManager.h"

namespace WeatherSmoke
{
	constexpr float StepSeconds = 0.05f;
	/** Game seconds to wait for the presenter's first application (the minimap hold is at most 4 s). */
	constexpr float ApplyTimeoutSeconds = 12.f;
	/** Game seconds after a Codex.Env.Visuals toggle (the presenter polls it every frame). */
	constexpr float ToggleSettleSeconds = 0.25f;

	enum class EStage : uint8
	{
		WaitApplying,
		VisualsOff,
		VisualsOn,
	};

	struct FState
	{
		EStage Stage = EStage::WaitApplying;
		float RunStart = -1.f;
		float StageStart = 0.f;
		float StageTime = 0.f;
		float TotalTime = 0.f;
		int32 Failures = 0;
		float BlizzardFogDensity = 0.f;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[WeatherSmoke] %s: %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	void Finish(FState& State, const TCHAR* Name)
	{
		const bool bPass = State.Failures == 0;
		UE_LOG(LogCodexTactics, Display, TEXT("[WeatherSmoke] RESULT %s (%d failed checks)"), bPass ? TEXT("PASS") : TEXT("FAIL"), State.Failures);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), bPass ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, Name);
	}

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

	/** Waits for the presenter's first application; false while waiting. Logs + checks a timeout. */
	AWeatherPresenterActor* WaitForPresenter(FState& State, UWorld& World, bool& bOutTimedOut)
	{
		bOutTimedOut = false;
		AWeatherPresenterActor* Presenter = AWeatherPresenterActor::Find(&World);
		if (Presenter && Presenter->IsApplying() && Presenter->GetApplyCount() > 0)
		{
			return Presenter;
		}
		if (State.TotalTime > ApplyTimeoutSeconds)
		{
			bOutTimedOut = true;
			Check(State, false, FString::Printf(TEXT("presenter spawned and applying within %.0f game s (found %d, applying %d)"), ApplyTimeoutSeconds,
				Presenter ? 1 : 0, Presenter && Presenter->IsApplying() ? 1 : 0));
		}
		return nullptr;
	}

	float FogDensityOf(const AWeatherPresenterActor& Presenter)
	{
		const AExponentialHeightFog* Fog = Presenter.GetHeightFog();
		return Fog ? Fog->GetComponent()->FogDensity : Presenter.GetLastFrame().Preset.FogDensity;
	}

	bool CloudsOff(const UWorld& World)
	{
		for (TActorIterator<AVolumetricCloud> It(const_cast<UWorld*>(&World)); It; ++It)
		{
			const UVolumetricCloudComponent* Component = It->FindComponentByClass<UVolumetricCloudComponent>();
			if (!It->IsHidden() || (Component && (Component->IsVisible() || Component->IsActive())))
			{
				return false;
			}
		}
		return true;
	}

	/** Stage 1: what was adopted. */
	void CheckAdoption(FState& State, const UWorld& World, const AWeatherPresenterActor& Presenter)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[WeatherSmoke] adopted: %s"), *Presenter.DescribeAdopted());
		int32 Placed = 0;
		for (TActorIterator<AWeatherPresenterActor> It(const_cast<UWorld*>(&World)); It; ++It)
		{
			++Placed;
		}
		Check(State, Placed == 1, FString::Printf(TEXT("1 exactly one presenter in the world (%d)"), Placed));
		Check(State, Presenter.GetSun() != nullptr, TEXT("1 the map's sun (directional light) adopted"));
		Check(State, Presenter.GetSkyAtmosphere() != nullptr, TEXT("1 the map's sky atmosphere adopted"));
		Check(State, Presenter.GetSkyLight() != nullptr, TEXT("1 the map's sky light adopted"));
		Check(State, Presenter.GetHeightFog() != nullptr, TEXT("1 the map's height fog adopted"));
		Check(State, Presenter.GetActiveMoon() != nullptr, FString::Printf(TEXT("1 a moon light (%s)"), Presenter.IsMoonOwn() ? TEXT("own") : TEXT("adopted")));
		Check(State, Presenter.GetAdoptedPostProcess() || (Presenter.GetOwnPostProcess() && Presenter.GetOwnPostProcess()->bEnabled),
			TEXT("1 a post-process is driven (adopted unbound volume or the own component, enabled)"));
		Check(State, Presenter.GetCloudCount() == 0 || CloudsOff(World),
			FString::Printf(TEXT("1 the map's volumetric clouds are off (%d cloud actor(s))"), Presenter.GetCloudCount()));
	}

	/** Stage 2 + 3 (synchronous: SetHour / ForceWeather broadcast OnStateSet, the presenter applies at once). */
	void CheckHoursAndWeather(FState& State, UEnvironmentSubsystem& Env, AWeatherPresenterActor& Presenter)
	{
		Env.SetClockScale(0.f);
		const UDirectionalLightComponent* Sun = Presenter.GetSun() ? Presenter.GetSun()->GetComponent() : nullptr;
		const UDirectionalLightComponent* Moon = Presenter.GetActiveMoon();

		Env.SetHour(12.f);
		const FWeatherVisualFrame Noon = Presenter.GetLastFrame();
		Check(State, Noon.SunElevation > 20.f && FMath::IsNearlyEqual(Noon.Hour, 12.f, 0.01f),
			FString::Printf(TEXT("2 12:00: sun %.1f deg above the horizon"), Noon.SunElevation));
		if (Sun)
		{
			const bool bMovable = Sun->Mobility == EComponentMobility::Movable;
			Check(State, !bMovable || Sun->GetComponentRotation().Pitch < -20.f,
				FString::Printf(TEXT("2 12:00: the sun light points down (pitch %.1f, %s)"), Sun->GetComponentRotation().Pitch, bMovable ? TEXT("movable") : TEXT("not movable: not rotated")));
			Check(State, Sun->IsVisible() && Sun->Intensity > 0.f, FString::Printf(TEXT("2 12:00: the sun is lit (%.2f, authored %.2f)"), Sun->Intensity, Presenter.GetAuthored().SunIntensity));
		}
		Check(State, Moon && !Moon->IsVisible(), TEXT("2 12:00: the moon is hidden"));

		Env.SetHour(23.f);
		const FWeatherVisualFrame Night = Presenter.GetLastFrame();
		Check(State, Night.SunElevation < 0.f && !Night.bSunVisible, FString::Printf(TEXT("2 23:00: sun %.1f deg, below the horizon, hidden"), Night.SunElevation));
		if (Sun)
		{
			Check(State, !Sun->IsVisible(), TEXT("2 23:00: the sun light is off"));
		}
		Check(State, Night.bMoonVisible && Night.MoonElevation > 0.f && Moon && Moon->IsVisible() && Moon->Intensity > 0.f,
			FString::Printf(TEXT("2 23:00: the moon is lit (%.2f) at %.1f deg"), Moon ? Moon->Intensity : 0.f, Night.MoonElevation));
		Check(State, !Moon || Moon->GetComponentRotation().Pitch < -10.f, FString::Printf(TEXT("2 23:00: the moon light points down (pitch %.1f)"),
			Moon ? Moon->GetComponentRotation().Pitch : 0.f));
		Check(State, Night.NightAmount > 0.99f, FString::Printf(TEXT("2 23:00: full night grade (%.2f)"), Night.NightAmount));

		Env.ForceWeather(EWeatherState::Clear, true);
		const float ClearFog = FogDensityOf(Presenter);
		Check(State, FMath::IsNearlyEqual(ClearFog, Presenter.GetPreset(EWeatherState::Clear).FogDensity, 1.e-4f),
			FString::Printf(TEXT("3 Clear: fog density %.4f = the Clear preset (%s)"), ClearFog, Presenter.IsPresetFromAsset(EWeatherState::Clear) ? TEXT("asset") : TEXT("C++ default")));
		Env.ForceWeather(EWeatherState::Blizzard, true);
		State.BlizzardFogDensity = FogDensityOf(Presenter);
		Check(State, State.BlizzardFogDensity > ClearFog, FString::Printf(TEXT("3 Blizzard: fog density %.4f > Clear %.4f"), State.BlizzardFogDensity, ClearFog));
		if (const AExponentialHeightFog* Fog = Presenter.GetHeightFog())
		{
			Check(State, Fog->GetComponent()->bEnableVolumetricFog == Presenter.GetPreset(EWeatherState::Blizzard).bVolumetricFog,
				TEXT("3 Blizzard: volumetric fog as the preset says"));
		}
		Env.SetHour(12.f);
		Check(State, Presenter.GetLastFrame().SunIntensityScale < Noon.SunIntensityScale,
			FString::Printf(TEXT("3 Blizzard noon sun x%.2f < Clear noon x%.2f"), Presenter.GetLastFrame().SunIntensityScale, Noon.SunIntensityScale));
	}

	/** Stage 4a: Codex.Env.Visuals 0 restored the authored values. */
	void CheckRestored(FState& State, const UWorld& World, const AWeatherPresenterActor& Presenter)
	{
		const FWeatherAuthoredLighting& Authored = Presenter.GetAuthored();
		Check(State, !Presenter.IsApplying(), TEXT("4 Codex.Env.Visuals 0: the presenter stopped"));
		if (const ADirectionalLight* SunActor = Presenter.GetSun())
		{
			const UDirectionalLightComponent* Sun = SunActor->GetComponent();
			Check(State, FMath::IsNearlyEqual(Sun->Intensity, Authored.SunIntensity, 1.e-3f) && Sun->IsVisible() == Authored.bSunVisible,
				FString::Printf(TEXT("4 sun intensity %.3f = authored %.3f, visibility authored"), Sun->Intensity, Authored.SunIntensity));
			Check(State, Sun->GetComponentRotation().Equals(Authored.SunRotation, 0.01f),
				FString::Printf(TEXT("4 sun rotation %s = authored %s"), *Sun->GetComponentRotation().ToCompactString(), *Authored.SunRotation.ToCompactString()));
			Check(State, (Sun->bUseTemperature != 0) == Authored.bSunUseTemperature && FMath::IsNearlyEqual(Sun->LightSourceAngle, Authored.SunSourceAngle, 1.e-3f),
				TEXT("4 sun temperature flag / source angle authored"));
		}
		if (const AExponentialHeightFog* FogActor = Presenter.GetHeightFog())
		{
			const UExponentialHeightFogComponent* Fog = FogActor->GetComponent();
			Check(State, FMath::IsNearlyEqual(Fog->FogDensity, Authored.FogDensity, 1.e-5f) && FMath::IsNearlyEqual(Fog->FogHeightFalloff, Authored.FogHeightFalloff, 1.e-5f)
				&& Fog->FogInscatteringLuminance.Equals(Authored.FogInscattering, 1.e-4f) && Fog->bEnableVolumetricFog == Authored.bVolumetricFog,
				FString::Printf(TEXT("4 fog density %.4f = authored %.4f (falloff, colour, volumetric too)"), Fog->FogDensity, Authored.FogDensity));
		}
		if (Presenter.IsMoonOwn())
		{
			Check(State, !Presenter.GetActiveMoon()->IsVisible(), TEXT("4 the own moon is hidden"));
		}
		Check(State, Presenter.GetAdoptedPostProcess() || !Presenter.GetOwnPostProcess()->bEnabled, TEXT("4 the own post-process is off"));
		bool bCloudsAuthored = true;
		for (TActorIterator<AVolumetricCloud> It(const_cast<UWorld*>(&World)); It; ++It)
		{
			bCloudsAuthored &= !It->IsHidden();
		}
		Check(State, Presenter.GetCloudCount() == 0 || bCloudsAuthored, TEXT("4 the volumetric clouds are back as authored"));
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		UpdateTime(State, *World);
		UEnvironmentSubsystem* Env = UEnvironmentSubsystem::Get(World);
		if (!Env)
		{
			Check(State, false, TEXT("environment subsystem present"));
			Finish(State, TEXT("WeatherSmoke"));
			return false;
		}
		switch (State.Stage)
		{
		case EStage::WaitApplying:
		{
			bool bTimedOut = false;
			AWeatherPresenterActor* Presenter = WaitForPresenter(State, *World, bTimedOut);
			if (bTimedOut)
			{
				Finish(State, TEXT("WeatherSmoke"));
				return false;
			}
			if (!Presenter)
			{
				return true;
			}
			UE_LOG(LogCodexTactics, Display, TEXT("[WeatherSmoke] start: %s; first application after %.2f game s"), *Env->Describe(), State.TotalTime);
			CheckAdoption(State, *World, *Presenter);
			CheckHoursAndWeather(State, *Env, *Presenter);
			AWeatherPresenterActor::SetVisualsEnabled(false);
			NextStage(State, EStage::VisualsOff);
			return true;
		}
		case EStage::VisualsOff:
			if (State.StageTime >= ToggleSettleSeconds)
			{
				if (const AWeatherPresenterActor* Presenter = AWeatherPresenterActor::Find(World))
				{
					CheckRestored(State, *World, *Presenter);
				}
				AWeatherPresenterActor::SetVisualsEnabled(true);
				NextStage(State, EStage::VisualsOn);
			}
			return true;
		case EStage::VisualsOn:
			if (State.StageTime >= ToggleSettleSeconds)
			{
				if (const AWeatherPresenterActor* Presenter = AWeatherPresenterActor::Find(World))
				{
					Check(State, Presenter->IsApplying() && FMath::IsNearlyEqual(FogDensityOf(*Presenter), State.BlizzardFogDensity, 1.e-4f),
						FString::Printf(TEXT("4 Codex.Env.Visuals 1: applied again (fog %.4f = Blizzard)"), FogDensityOf(*Presenter)));
					Check(State, Presenter->GetCloudCount() == 0 || CloudsOff(*World), TEXT("4 the volumetric clouds are off again"));
				}
				Env->SetClockScale(1.f);
				Finish(State, TEXT("WeatherSmoke"));
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
		TEXT("CodexTactics.WeatherSmoke"),
		TEXT("Dev check: the weather presenter adopts the map's lights, sun / moon by hour, fog by weather, Codex.Env.Visuals 0 restores the authored lighting; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

namespace WeatherLakeSmoke
{
	using namespace WeatherSmoke;

	bool LakeStep(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		UpdateTime(State, *World);
		const UEnvironmentSubsystem* Env = UEnvironmentSubsystem::Get(World);
		bool bTimedOut = false;
		AWeatherPresenterActor* Presenter = Env ? WaitForPresenter(State, *World, bTimedOut) : nullptr;
		if (!Env || bTimedOut)
		{
			Check(State, Env != nullptr, TEXT("lake: environment subsystem present"));
			Finish(State, TEXT("WeatherLakeSmoke"));
			return false;
		}
		if (!Presenter)
		{
			return true;
		}
		UE_LOG(LogCodexTactics, Display, TEXT("[WeatherSmoke] lake start: %s; adopted: %s"), *Env->Describe(), *Presenter->DescribeAdopted());
		Check(State, Env->IsLocked() && Env->GetWeather() == EWeatherState::Snowfall && FMath::IsNearlyEqual(Env->GetHour(), 23.f, 0.01f),
			TEXT("L1 the lake is locked at 23:00 Snowfall"));
		const FWeatherVisualFrame& Frame = Presenter->GetLastFrame();
		Check(State, Frame.Weather == EWeatherState::Snowfall && FMath::IsNearlyEqual(Frame.Hour, 23.f, 0.01f),
			FString::Printf(TEXT("L1 the applied frame is 23:00 Snowfall (%.2f, %s)"), Frame.Hour, *EnvironmentRules::WeatherToString(Frame.Weather)));
		const UDirectionalLightComponent* Sun = Presenter->GetSun() ? Presenter->GetSun()->GetComponent() : nullptr;
		Check(State, Frame.SunElevation < 0.f && (!Sun || !Sun->IsVisible()), FString::Printf(TEXT("L2 night: the sun is below the horizon (%.1f deg) and off"), Frame.SunElevation));
		const UDirectionalLightComponent* Moon = Presenter->GetActiveMoon();
		Check(State, Moon && Moon->IsVisible() && Moon->Intensity > 0.f, FString::Printf(TEXT("L2 night: the moon lights the lake (%.2f)"), Moon ? Moon->Intensity : 0.f));
		Check(State, Frame.NightAmount > 0.99f, TEXT("L2 night: full night grade"));
		const float Fog = FogDensityOf(*Presenter);
		Check(State, Presenter->GetHeightFog() != nullptr, TEXT("L3 the lake's height fog adopted"));
		Check(State, FMath::IsNearlyEqual(Fog, Presenter->GetPreset(EWeatherState::Snowfall).FogDensity, 1.e-4f)
			&& Fog > Presenter->GetPreset(EWeatherState::Clear).FogDensity,
			FString::Printf(TEXT("L3 Snowfall fog %.4f (> Clear %.4f)"), Fog, Presenter->GetPreset(EWeatherState::Clear).FogDensity));
		Check(State, Presenter->GetAdoptedPostProcess() || Presenter->GetOwnPostProcess()->bEnabled, TEXT("L3 the night / snowfall grade is applied (post-process on)"));
		Check(State, Presenter->GetCloudCount() == 0 || CloudsOff(*World), FString::Printf(TEXT("L3 volumetric clouds off (%d)"), Presenter->GetCloudCount()));
		Finish(State, TEXT("WeatherLakeSmoke"));
		return false;
	}

	void LakeRun(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return LakeStep(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.WeatherLakeSmoke"),
		TEXT("Dev check: on the Demo 01 lake (locked 23:00 Snowfall) the weather presenter shows night (moon, no sun) and the Snowfall fog; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&LakeRun));
}

#endif // !UE_BUILD_SHIPPING
