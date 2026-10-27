// Dev-only headless checks of the level START look and the weather presenter's own lighting rig (docs/systems/environment-weather.md
// section 16, game designer 2026-10-25: "When I press Play, first a sunset shows, then it switches to night. The snow also starts falling
// with a delay ... Make it appear at once."; UE-only, no Godot reference).
//   CodexTactics.StartLightingSmoke (nullrhi; the lake, -LevelJson=L_Demo01_A_FrozenLake.json):
//     A. at the command (the first engine tick after the map loaded, before the world ticked): the presenter is applying and has applied
//        the 23:00 Snowfall state - sun / moon by the frame, MPC SnowAmount = the state, snapped by camera cuts at stable eye adaptation
//        speeds (section 19: no 1000 f-stops/s window), the falling snow
//        intensity already at its target (no ramp); the rig mode is logged (adopted with the map's lights, own after the cleanup).
//     B. the level's lights are destroyed and the presenter re-spawned: in the same frame it builds its OWN rig (sun, sky atmosphere, sky
//        light, height fog, moon, post-process), applies the night, 12:00 lights the own sun; Codex.Env.Visuals 0 leaves the own rig at its
//        daylight baseline (never black), 1 applies again.
//     C. a state jump (SetState, as a save load) snaps again (camera-cut frames, snow at the new target); ForceWeather does not (smoothing).
//   CodexTactics.StartSnowSmoke (rendered; the lake): the falling snow is active from the first frames, pre-warmed once at the level start
//     (GetWarmupCount 1, simulated >= the flake life) at its full target intensity, particles in the air; the start cover (black until
//     camera + snow are ready) was shown and is lifted; a weather change afterwards (ForceWeather Blizzard) fades in (current < target)
//     without a second warm-up.
// Both print "[StartLightingSmoke] PASS/FAIL ..." and "Smoke RESULT: PASS|FAIL", then exit.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "CodexTactics.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/DirectionalLightComponent.h"
#include "GameFramework/PlayerController.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Containers/Ticker.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Environment/EnvironmentRules.h"
#include "Environment/EnvironmentStartRules.h"
#include "Environment/EnvironmentSubsystem.h"
#include "Environment/WeatherPresenterActor.h"
#include "Environment/WeatherSnowFxComponent.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "NiagaraComponent.h"

namespace StartLightingSmoke
{
	struct FState
	{
		int32 Failures = 0;
		int32 Frame = 0;
		int32 StageFrame = 0;
		int32 Stage = 0;
		int32 SerialBefore = 0;
		TWeakObjectPtr<AWeatherPresenterActor> OwnPresenter;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[StartLightingSmoke] %s: %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	void Finish(FState& State, const TCHAR* Name)
	{
		const bool bPass = State.Failures == 0;
		UE_LOG(LogCodexTactics, Display, TEXT("[StartLightingSmoke] %s RESULT %s (%d failed checks)"), Name, bPass ? TEXT("PASS") : TEXT("FAIL"), State.Failures);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), bPass ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, Name);
	}

	void SetVisuals(int32 Value)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Env.Visuals")))
		{
			Variable->Set(Value, ECVF_SetByConsole);
		}
	}

	/** The applied frame matches the lights: the sun / moon visibility and the movable sun's pitch. */
	void CheckFrameOnLights(FState& State, const AWeatherPresenterActor& Presenter, const TCHAR* Tag)
	{
		const FWeatherVisualFrame& Frame = Presenter.GetLastFrame();
		const UDirectionalLightComponent* Sun = Presenter.GetSunLight();
		const UDirectionalLightComponent* Moon = Presenter.GetActiveMoon();
		Check(State, Sun != nullptr, FString::Printf(TEXT("%s a sun light is bound (%s)"), Tag, Presenter.GetSun() ? TEXT("adopted") : TEXT("own")));
		if (Sun)
		{
			const bool bMovable = Sun->Mobility == EComponentMobility::Movable;
			Check(State, Sun->IsVisible() == Frame.bSunVisible, FString::Printf(TEXT("%s sun visibility %d = the frame's %d (%.1f deg)"), Tag,
				Sun->IsVisible() ? 1 : 0, Frame.bSunVisible ? 1 : 0, Frame.SunElevation));
			Check(State, !bMovable || FMath::IsNearlyEqual(Sun->GetComponentRotation().Pitch, -Frame.SunElevation, 0.2f),
				FString::Printf(TEXT("%s sun pitch %.1f = -elevation %.1f (no authored sunset pose)"), Tag, Sun->GetComponentRotation().Pitch, -Frame.SunElevation));
		}
		Check(State, Moon && Moon->IsVisible() == Frame.bMoonVisible, FString::Printf(TEXT("%s moon visibility = the frame's (%d)"), Tag, Frame.bMoonVisible ? 1 : 0));
	}

	/** Stage A: everything was applied before the world ticked. */
	void CheckStart(FState& State, UWorld& World)
	{
		const UEnvironmentSubsystem* Env = UEnvironmentSubsystem::Get(&World);
		AWeatherPresenterActor* Presenter = AWeatherPresenterActor::Find(&World);
		Check(State, Env && Env->IsInitialised(), TEXT("A environment initialised at the command"));
		Check(State, Presenter && Presenter->IsApplying() && Presenter->GetApplyCount() >= 1 && Presenter->GetFirstApplyGameTime() == 0.0,
			FString::Printf(TEXT("A the presenter applied in BeginPlay, before the world's first tick (first application at game %.3f s; now %.3f s, %d applications)"),
				Presenter ? Presenter->GetFirstApplyGameTime() : -1.0, World.GetTimeSeconds(), Presenter ? Presenter->GetApplyCount() : 0));
		if (!Env || !Presenter)
		{
			return;
		}
		UE_LOG(LogCodexTactics, Display, TEXT("[StartLightingSmoke] start: %s; %s"), *Env->Describe(), *Presenter->DescribeAdopted());
		const FWeatherVisualFrame& Frame = Presenter->GetLastFrame();
		Check(State, FMath::IsNearlyEqual(Frame.Hour, Env->GetHour(), 0.01f) && Frame.Weather == Env->GetWeather(),
			FString::Printf(TEXT("A the applied frame is the start state (%.2f %s)"), Frame.Hour, *EnvironmentRules::WeatherToString(Frame.Weather)));
		CheckFrameOnLights(State, *Presenter, TEXT("A"));
		float MpcSnow = -1.f;
		const bool bMpcRead = Presenter->GetEnvironmentScalar(TEXT("SnowAmount"), MpcSnow);
		Check(State, bMpcRead && FMath::IsNearlyEqual(MpcSnow, Env->GetSnowAmount(), 1.e-3f),
			FString::Printf(TEXT("A MPC SnowAmount %.3f = the state %.3f (read %d)"), MpcSnow, Env->GetSnowAmount(), bMpcRead ? 1 : 0));
		const UPostProcessComponent* Post = Presenter->GetOwnPostProcess();
		const FPostProcessSettings& Settings = Presenter->GetAdoptedPostProcess() ? Presenter->GetAdoptedPostProcess()->Settings : Post->Settings;
		// Section 19 (start flicker): the snap is the camera-cut frames (the engine's eye adaptation ForceTarget), never a fast speed window.
		Check(State, Presenter->GetSecondsSinceSnap() >= 0.f && !Presenter->IsExposureSnapping()
			&& EnvironmentStartRules::GetExposureFrameClosure(Settings.AutoExposureSpeedUp, 1.f / 60.f) <= EnvironmentStartRules::MaxStableExposureClosure
			&& EnvironmentStartRules::GetExposureFrameClosure(Settings.AutoExposureSpeedDown, 1.f / 60.f) <= EnvironmentStartRules::MaxStableExposureClosure,
			FString::Printf(TEXT("A snapped (%.2f s ago) by camera cuts, the eye adaptation at stable speeds (up %.1f down %.1f f-stops/s, no 1000 window)"),
				Presenter->GetSecondsSinceSnap(), Settings.AutoExposureSpeedUp, Settings.AutoExposureSpeedDown));
		const UWeatherSnowFxComponent* Snow = Presenter->GetSnowFx();
		Check(State, Snow && Snow->GetTargetIntensity() > 0.f && FMath::IsNearlyEqual(Snow->GetCurrentIntensity(), Snow->GetTargetIntensity(), 1.e-4f),
			FString::Printf(TEXT("A the falling snow is at its target from the start (%.2f / %.2f, no ramp)"), Snow ? Snow->GetCurrentIntensity() : -1.f,
				Snow ? Snow->GetTargetIntensity() : -1.f));
		Check(State, true, FString::Printf(TEXT("A rig mode %s"), *EnvironmentStartRules::RigModeToString(Presenter->GetRigMode())));
	}

	/** Stage B: the level without its lights -> the own rig, applied in the spawn frame. */
	void StartOwnRig(FState& State, UWorld& World)
	{
		int32 Destroyed = 0;
		TArray<AActor*> Doomed;
		for (TActorIterator<AActor> It(&World); It; ++It)
		{
			AActor* Actor = *It;
			const APostProcessVolume* Volume = Cast<APostProcessVolume>(Actor);
			if (Actor->IsA<ADirectionalLight>() || Actor->IsA<ASkyLight>() || Actor->IsA<ASkyAtmosphere>() || Actor->IsA<AExponentialHeightFog>()
				|| Actor->IsA<AVolumetricCloud>() || (Volume && Volume->bUnbound) || Actor->IsA<AWeatherPresenterActor>())
			{
				Doomed.Add(Actor);
			}
		}
		for (AActor* Actor : Doomed)
		{
			Destroyed += Actor->Destroy() ? 1 : 0;
		}
		AWeatherPresenterActor* Presenter = AWeatherPresenterActor::SpawnForWorld(World, true);
		State.OwnPresenter = Presenter;
		Check(State, Presenter != nullptr, FString::Printf(TEXT("B the level's lights + presenter destroyed (%d actors), a new presenter spawned"), Destroyed));
		if (!Presenter)
		{
			return;
		}
		UE_LOG(LogCodexTactics, Display, TEXT("[StartLightingSmoke] own rig: %s"), *Presenter->DescribeAdopted());
		Check(State, Presenter->GetRigMode() == EWeatherRigMode::Own, FString::Printf(TEXT("B rig mode own (%s)"), *EnvironmentStartRules::RigModeToString(Presenter->GetRigMode())));
		Check(State, Presenter->GetSun() == nullptr && Presenter->GetSunLight() && Presenter->GetSkyAtmosphereComponent() && Presenter->GetSkyLightComponent()
			&& Presenter->GetHeightFogComponent(), TEXT("B own sun, sky atmosphere, sky light and height fog bound"));
		Check(State, Presenter->GetSkyAtmosphereComponent() && Presenter->GetSkyAtmosphereComponent()->IsVisible()
			&& Presenter->GetSkyLightComponent()->IsVisible() && Presenter->GetHeightFogComponent()->IsVisible(), TEXT("B the own sky / sky light / fog are shown"));
		Check(State, Presenter->IsApplying() && Presenter->GetApplyCount() >= 1 && Presenter->GetFirstApplyGameTime() == World.GetTimeSeconds(),
			TEXT("B applied in its spawn frame (BeginPlay)"));
		CheckFrameOnLights(State, *Presenter, TEXT("B night"));
		const UDirectionalLightComponent* Moon = Presenter->GetActiveMoon();
		Check(State, Moon && Moon->IsVisible() && Moon->Intensity > 0.f, FString::Printf(TEXT("B the own moon lights the night (%.2f)"), Moon ? Moon->Intensity : 0.f));
		Check(State, Presenter->GetSkyLightComponent()->Intensity > 0.f, FString::Printf(TEXT("B the own sky light is lit (%.2f)"), Presenter->GetSkyLightComponent()->Intensity));
		Check(State, Presenter->GetOwnPostProcess() && Presenter->GetOwnPostProcess()->bEnabled, TEXT("B the own post-process grades the night"));
		UEnvironmentSubsystem* Env = UEnvironmentSubsystem::Get(&World);
		const float Fog = Presenter->GetHeightFogComponent()->FogDensity;
		Check(State, Env && FMath::IsNearlyEqual(Fog, Presenter->GetPreset(Env->GetWeather()).FogDensity, 1.e-4f),
			FString::Printf(TEXT("B the own fog follows the weather preset (%.4f)"), Fog));
		if (Env)
		{
			const float Hour = Env->GetHour();
			Env->SetHour(12.f);
			const UDirectionalLightComponent* Sun = Presenter->GetSunLight();
			Check(State, Sun && Sun->IsVisible() && Sun->Intensity > 0.f && Sun->GetComponentRotation().Pitch < -20.f,
				FString::Printf(TEXT("B 12:00: the own sun is up and lit (%.2f lux, pitch %.1f)"), Sun ? Sun->Intensity : 0.f, Sun ? Sun->GetComponentRotation().Pitch : 0.f));
			Env->SetHour(Hour);
		}
		SetVisuals(0);
	}

	void CheckVisualsOff(FState& State)
	{
		const AWeatherPresenterActor* Presenter = State.OwnPresenter.Get();
		if (!Presenter)
		{
			Check(State, false, TEXT("B the own presenter survived"));
			return;
		}
		const UDirectionalLightComponent* Sun = Presenter->GetSunLight();
		const FWeatherAuthoredLighting& Authored = Presenter->GetAuthored();
		Check(State, !Presenter->IsApplying(), TEXT("B Codex.Env.Visuals 0: the presenter stopped"));
		Check(State, Sun && Sun->IsVisible() && FMath::IsNearlyEqual(Sun->Intensity, Authored.SunIntensity, 1.e-3f) && Authored.SunIntensity > 0.f,
			FString::Printf(TEXT("B visuals off: the own sun at its baseline (%.2f lux, visible) - never a black level"), Sun ? Sun->Intensity : 0.f));
		Check(State, Presenter->GetSkyLightComponent() && Presenter->GetSkyLightComponent()->IsVisible(), TEXT("B visuals off: the own sky light stays"));
		SetVisuals(1);
	}

	void CheckSnap(FState& State, UWorld& World)
	{
		AWeatherPresenterActor* Presenter = State.OwnPresenter.Get();
		UEnvironmentSubsystem* Env = UEnvironmentSubsystem::Get(&World);
		Check(State, Presenter && Presenter->IsApplying(), TEXT("B Codex.Env.Visuals 1: applying again"));
		if (!Presenter || !Env)
		{
			return;
		}
		// A load-like jump: Blizzard at 14:00 -> snap (exposure window restarts, the snow jumps to the blizzard).
		FEnvironmentState Jump = Env->GetStateRef();
		Jump.Weather = EWeatherState::Blizzard;
		Jump.NextWeather = EWeatherState::Snowfall;
		Jump.WeatherElapsedSeconds = 0.f;
		Jump.Hour = 14.f;
		const int32 SerialBefore = Env->GetSnapSerial();
		Env->SetState(Jump);
		const UWeatherSnowFxComponent* Snow = Presenter->GetSnowFx();
		Check(State, Env->GetSnapSerial() == SerialBefore + 1, TEXT("C SetState moves the snap serial"));
		Check(State, Presenter->GetSecondsSinceSnap() < 0.05f && Presenter->GetCameraCutFramesLeft() > 0,
			FString::Printf(TEXT("C a state jump snaps the exposure again (%.3f s ago, %d camera-cut frames)"), Presenter->GetSecondsSinceSnap(),
				Presenter->GetCameraCutFramesLeft()));
		Check(State, Snow && FMath::IsNearlyEqual(Snow->GetCurrentIntensity(), Snow->GetTargetIntensity(), 1.e-4f) && Snow->GetTargetIntensity() > 0.9f,
			FString::Printf(TEXT("C the snow jumps to the blizzard (%.2f / %.2f)"), Snow ? Snow->GetCurrentIntensity() : -1.f, Snow ? Snow->GetTargetIntensity() : -1.f));
		const float Before = Snow ? Snow->GetCurrentIntensity() : 0.f;
		Env->ForceWeather(EWeatherState::Clear, true);
		Check(State, Env->GetSnapSerial() == SerialBefore + 1, TEXT("C ForceWeather is not a jump (serial unchanged)"));
		Check(State, Snow && Snow->GetTargetIntensity() < 0.01f && Snow->GetCurrentIntensity() > 0.5f && Snow->GetCurrentIntensity() <= Before,
			FString::Printf(TEXT("C a weather change in play fades (current %.2f -> target %.2f)"), Snow ? Snow->GetCurrentIntensity() : -1.f,
				Snow ? Snow->GetTargetIntensity() : -1.f));
	}

	bool LightingStep(TWeakObjectPtr<UWorld> WeakWorld, TSharedRef<FState> State)
	{
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		++State->Frame;
		++State->StageFrame;
		switch (State->Stage)
		{
		case 0:
			if (State->StageFrame >= 10)
			{
				StartOwnRig(*State, *World);
				State->Stage = 1;
				State->StageFrame = 0;
			}
			return true;
		case 1:
			if (State->StageFrame >= 5)
			{
				CheckVisualsOff(*State);
				State->Stage = 2;
				State->StageFrame = 0;
			}
			return true;
		default:
			if (State->StageFrame >= 5)
			{
				CheckSnap(*State, *World);
				Finish(*State, TEXT("StartLightingSmoke"));
				return false;
			}
			return true;
		}
	}

	void Run(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TSharedRef<FState> State = MakeShared<FState>();
		CheckStart(*State, *World);
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return LightingStep(WeakWorld, State);
		}));
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.StartLightingSmoke"),
		TEXT("Dev check (section 16): the level's start state is applied before the first frame (no authored sunset, exposure snap, snow at its target), the presenter's own rig lights a level without lights; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

namespace StartSnowSmoke
{
	using StartLightingSmoke::Check;
	using StartLightingSmoke::FState;

	bool SnowStep(TWeakObjectPtr<UWorld> WeakWorld, TSharedRef<FState> State)
	{
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		++State->Frame;
		AWeatherPresenterActor* Presenter = AWeatherPresenterActor::Find(World);
		UWeatherSnowFxComponent* Snow = Presenter ? Presenter->GetSnowFx() : nullptr;
		UEnvironmentSubsystem* Env = UEnvironmentSubsystem::Get(World);
		if (State->Stage == 0)
		{
			// The warmed-up activation runs in the first world tick after the camera update; allow a few frames for the command order.
			if (State->Frame < 15 && (!Snow || Snow->IsWarmupActivationPending() || !Snow->IsSnowActive() || Presenter->IsStartCoverOn()))
			{
				return true;
			}
			Check(*State, FApp::CanEverRender(), TEXT("rendered run (the snow FX never spawns without a renderer)"));
			Check(*State, Snow && Snow->IsSnowActive(), FString::Printf(TEXT("the falling snow is active by frame %d"), State->Frame));
			Check(*State, Snow && Snow->GetWarmupCount() == 1, FString::Printf(TEXT("pre-warmed once at the level start (%d)"), Snow ? Snow->GetWarmupCount() : -1));
			Check(*State, Snow && Snow->GetLastWarmupSeconds() >= 5.f && Snow->GetLastWarmupTicks() > 0,
				FString::Printf(TEXT("the warm-up simulated the flake life (%.1f s, %d ticks)"), Snow ? Snow->GetLastWarmupSeconds() : 0.f, Snow ? Snow->GetLastWarmupTicks() : 0));
			Check(*State, Snow && Snow->GetTargetIntensity() > 0.f && FMath::IsNearlyEqual(Snow->GetCurrentIntensity(), Snow->GetTargetIntensity(), 1.e-4f),
				FString::Printf(TEXT("full target intensity from the start (%.2f / %.2f)"), Snow ? Snow->GetCurrentIntensity() : -1.f, Snow ? Snow->GetTargetIntensity() : -1.f));
			Check(*State, Snow && Snow->GetNiagara() && Snow->GetNiagara()->IsVisible(), TEXT("the snow Niagara component is visible"));
			Check(*State, Snow && Snow->GetParticleCount() > 0, FString::Printf(TEXT("particles already in the air (%s)"), Snow ? *Snow->DescribeParticles() : TEXT("-")));
			const APlayerController* PC = World->GetFirstPlayerController();
			const APlayerCameraManager* CameraManager = PC ? PC->PlayerCameraManager.Get() : nullptr;
			Check(*State, Presenter && Presenter->WasStartCoverShown() && !Presenter->IsStartCoverOn() && CameraManager && !CameraManager->bEnableFading,
				FString::Printf(TEXT("the start cover hid the first frames and is lifted - no camera fade left (%d presenter ticks)"),
					Presenter ? Presenter->GetStartCoverTicks() : -1));
			if (Env)
			{
				Env->ForceWeather(EWeatherState::Blizzard, true);
			}
			State->Stage = 1;
			State->StageFrame = 0;
			return true;
		}
		if (++State->StageFrame < 3)
		{
			return true;
		}
		Check(*State, Snow && Snow->GetTargetIntensity() > 0.9f && Snow->GetCurrentIntensity() < Snow->GetTargetIntensity(),
			FString::Printf(TEXT("a weather change in play fades in (%.2f -> %.2f)"), Snow ? Snow->GetCurrentIntensity() : -1.f, Snow ? Snow->GetTargetIntensity() : -1.f));
		Check(*State, Snow && Snow->GetWarmupCount() == 1 && Snow->IsSnowActive(), TEXT("no second warm-up for a weather change"));
		StartLightingSmoke::Finish(*State, TEXT("StartSnowSmoke"));
		return false;
	}

	void SnowRun(const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TSharedRef<FState> State = MakeShared<FState>();
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return SnowStep(WeakWorld, State);
		}));
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.StartSnowSmoke"),
		TEXT("Dev check (section 16, rendered): the falling snow is pre-warmed and at full density from the level's first frames; a weather change still fades in; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SnowRun));
}

#endif // !UE_BUILD_SHIPPING
