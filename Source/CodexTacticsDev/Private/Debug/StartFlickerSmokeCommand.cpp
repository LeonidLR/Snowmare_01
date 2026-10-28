// Dev-only regression check of the start flicker (docs/systems/environment-weather.md section 19; user report 2026-10-27: "a strange
// flicker when the level starts", "as if there's a thunderstorm" in the first ~1 s on any level). UE-only.
//   CodexTactics.StartFlickerSmoke [map ...] (RENDERED: smoke_parallel "rendered"; default maps: the lake, the factory, L_MovementTest,
//   L_WeatherRainTest). With Codex.Env.Lightning forced to 1 (so the automated-run suppression is not what keeps it dark), each map is
//   opened and its first FrameCount rendered frames (3 s at the smokes' 60 fps fixed step) are measured from the level's FIRST frame
//   (StartFlickerProbe, armed at PostLoadMapWithWorld):
//     1. no frame-to-frame mean-luma step above MaxMeanStep outside camera fades (the start cover / a title fade are intended): the old
//        1 s eye adaptation window at 1000 f-stops / s made the lake jump 17 <-> 237 every 2-4 frames;
//     2. no lightning in any of these frames: an arctic level has no rain FX at all; a temperate level that does not start in its Storm
//        never flashes (no strike, flash scale 0);
//     3. an arctic level forced into a Blizzard (the arctic storm rung) for LightningFrames frames still has no rain FX / no flash.
// Prints "[StartFlickerSmoke] PASS/FAIL ..." per check and "Smoke RESULT: PASS|FAIL", then exits.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "CodexTactics.h"
#include "Debug/StartFlickerProbe.h"
#include "Engine/World.h"
#include "Environment/EnvironmentRules.h"
#include "Environment/EnvironmentSubsystem.h"
#include "Environment/WeatherPresenterActor.h"
#include "Environment/WeatherRainFxComponent.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/CoreDelegates.h"
#include "UObject/UObjectGlobals.h"

namespace StartFlickerSmoke
{
	constexpr int32 FrameCount = 180;
	constexpr int32 LightningFrames = 60;
	/** Mean luma (0..255) step allowed between two frames: the UI panels that open with the tutorial step ~4; the flicker stepped 100-170. */
	constexpr float MaxMeanStep = 20.f;

	enum class EStage : uint8 { Opening, Recording, Blizzard, Done };

	struct FRunner
	{
		TArray<FString> Maps;
		int32 MapIndex = -1;
		EStage Stage = EStage::Opening;
		int32 StageFrames = 0;
		int32 Failures = 0;
		int32 FlashFrames = 0;
		StartFlickerProbe::FRecorder Recorder;
		FDelegateHandle EndFrameHandle;
		FDelegateHandle PostLoadHandle;
		FString CurrentMap;
	};

	TSharedPtr<FRunner> Runner;

	void Check(bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[StartFlickerSmoke] %s: %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What);
		Runner->Failures += bOk ? 0 : 1;
	}

	void Finish()
	{
		FCoreDelegates::OnEndFrame.Remove(Runner->EndFrameHandle);
		FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(Runner->PostLoadHandle);
		Runner->Recorder.End();
		const bool bPass = Runner->Failures == 0;
		UE_LOG(LogCodexTactics, Display, TEXT("[StartFlickerSmoke] RESULT %s (%d failed checks)"), bPass ? TEXT("PASS") : TEXT("FAIL"), Runner->Failures);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), bPass ? TEXT("PASS") : TEXT("FAIL"));
		Runner.Reset();
		FPlatformMisc::RequestExit(false, TEXT("StartFlickerSmoke"));
	}

	void OpenNext()
	{
		++Runner->MapIndex;
		if (!Runner->Maps.IsValidIndex(Runner->MapIndex))
		{
			Finish();
			return;
		}
		Runner->Stage = EStage::Opening;
		Runner->StageFrames = 0;
		Runner->FlashFrames = 0;
		Runner->CurrentMap = Runner->Maps[Runner->MapIndex];
		UE_LOG(LogCodexTactics, Display, TEXT("[StartFlickerSmoke] opening %s"), *Runner->CurrentMap);
		if (UWorld* World = StartFlickerProbe::FindPlayWorld())
		{
			UGameplayStatics::OpenLevel(World, FName(*Runner->CurrentMap));
		}
	}

	/** The new map loaded (after its BeginPlay, before its first draw): measure from its first frame. */
	void OnPostLoadMap(UWorld* World)
	{
		if (!Runner.IsValid() || Runner->Stage != EStage::Opening || !World || !World->IsGameWorld())
		{
			return;
		}
		Runner->Stage = EStage::Recording;
		Runner->StageFrames = 0;
		Runner->Recorder.Begin(FrameCount, FString(), FString());
	}

	/** Lightning state of this frame: an arctic level has no rain FX; a temperate one only flashes in its Storm. */
	void CheckLightningFrame(UWorld* World, bool bExpectNone)
	{
		const AWeatherPresenterActor* Presenter = World ? AWeatherPresenterActor::Find(World) : nullptr;
		const UWeatherRainFxComponent* Rain = Presenter ? Presenter->GetRainFx() : nullptr;
		if (Rain && bExpectNone && (Rain->GetFlashScale() > 0.f || Rain->GetStrikeCount() > 0 || Rain->IsLightningRunning()))
		{
			++Runner->FlashFrames;
		}
	}

	void EvaluateRecording(UWorld* World)
	{
		const UEnvironmentSubsystem* Env = World ? UEnvironmentSubsystem::Get(World) : nullptr;
		const AWeatherPresenterActor* Presenter = World ? AWeatherPresenterActor::Find(World) : nullptr;
		const bool bTemperate = Env && Env->GetClimate() == EClimateProfile::Temperate;
		const TArray<StartFlickerProbe::FFrameStats>& Stats = Runner->Recorder.GetStats();
		int32 StepFrame = 0;
		const float Step = Runner->Recorder.MaxMeanStep(1, Stats.Num(), StepFrame);
		int32 Visible = 0;
		float MinMean = 255.f;
		float MaxMean = 0.f;
		for (const StartFlickerProbe::FFrameStats& F : Stats)
		{
			if (F.FadeAmount <= 0.001f)
			{
				++Visible;
				MinMean = FMath::Min(MinMean, F.Mean);
				MaxMean = FMath::Max(MaxMean, F.Mean);
			}
		}
		Check(Stats.Num() == FrameCount, FString::Printf(TEXT("%s: %d / %d rendered frames measured from the level's first frame"), *Runner->CurrentMap, Stats.Num(), FrameCount));
		Check(Step <= MaxMeanStep, FString::Printf(TEXT("%s: no start flicker - largest frame-to-frame mean-luma step %.2f (frame %d) <= %.0f over %d visible frames (mean %.1f..%.1f)"),
			*Runner->CurrentMap, Step, StepFrame, MaxMeanStep, Visible, MinMean, MaxMean));
		Check(Presenter && Presenter->GetCameraCutFramesLeft() == 0 && !Presenter->IsExposureSnapping(),
			FString::Printf(TEXT("%s: the start snap is over (camera cuts done, no fast exposure window)"), *Runner->CurrentMap));
		const bool bStormStart = Env && Env->GetWeather() == EWeatherState::Blizzard;
		if (!bTemperate)
		{
			Check(Presenter && !Presenter->GetRainFx(), FString::Printf(TEXT("%s (arctic): no rain FX / lightning flash light at all"), *Runner->CurrentMap));
		}
		Check(Runner->FlashFrames == 0 || (bTemperate && bStormStart),
			FString::Printf(TEXT("%s (%s, start %s): no lightning in the first %d frames (%d flash frames)"), *Runner->CurrentMap,
				*EnvironmentRules::ClimateToString(Env ? Env->GetClimate() : EClimateProfile::Arctic),
				Env ? *EnvironmentRules::WeatherToString(Env->GetWeather()) : TEXT("-"), FrameCount, Runner->FlashFrames));
	}

	void OnEndFrame()
	{
		if (!Runner.IsValid())
		{
			return;
		}
		UWorld* World = StartFlickerProbe::FindPlayWorld();
		switch (Runner->Stage)
		{
		case EStage::Opening:
			return;
		case EStage::Recording:
		{
			const UEnvironmentSubsystem* Env = World ? UEnvironmentSubsystem::Get(World) : nullptr;
			const bool bTemperateStorm = Env && Env->GetClimate() == EClimateProfile::Temperate && Env->GetWeather() == EWeatherState::Blizzard;
			CheckLightningFrame(World, !bTemperateStorm);
			if (!Runner->Recorder.IsDone() && ++Runner->StageFrames < FrameCount * 4)
			{
				Runner->Recorder.RequestNext();
				return;
			}
			Runner->Recorder.End();
			EvaluateRecording(World);
			if (Env && Env->GetClimate() == EClimateProfile::Arctic)
			{
				// 3. The arctic storm rung with lightning forced on: still never a flash.
				if (UEnvironmentSubsystem* MutableEnv = UEnvironmentSubsystem::Get(World))
				{
					MutableEnv->ForceWeather(EWeatherState::Blizzard, true);
				}
				Runner->Stage = EStage::Blizzard;
				Runner->StageFrames = 0;
				Runner->FlashFrames = 0;
				return;
			}
			OpenNext();
			return;
		}
		case EStage::Blizzard:
		{
			CheckLightningFrame(World, true);
			const AWeatherPresenterActor* Presenter = World ? AWeatherPresenterActor::Find(World) : nullptr;
			if (Presenter && Presenter->GetRainFx())
			{
				++Runner->FlashFrames; // an arctic level must never even create the rain FX
			}
			if (++Runner->StageFrames < LightningFrames)
			{
				return;
			}
			const UEnvironmentSubsystem* Env = World ? UEnvironmentSubsystem::Get(World) : nullptr;
			Check(Env && Env->GetWeather() == EWeatherState::Blizzard && Runner->FlashFrames == 0,
				FString::Printf(TEXT("%s (arctic) forced into a Blizzard with Codex.Env.Lightning 1: no rain FX / flash in %d frames (%d bad frames)"),
					*Runner->CurrentMap, LightningFrames, Runner->FlashFrames));
			OpenNext();
			return;
		}
		default:
			return;
		}
	}

	void Run(const TArray<FString>& Args, UWorld* /*World*/)
	{
		Runner = MakeShared<FRunner>();
		Runner->Maps = Args;
		if (Runner->Maps.IsEmpty())
		{
			Runner->Maps = { TEXT("/Game/Maps/L_Demo01_A_FrozenLake"), TEXT("/Game/Maps/L_Demo01_B_FrozenFactory"), TEXT("/Game/Maps/L_MovementTest"),
				TEXT("/Game/Environment/Weather/Test/L_WeatherRainTest") };
		}
		if (IConsoleVariable* Lightning = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Env.Lightning")))
		{
			Lightning->Set(TEXT("1"), ECVF_SetByConsole); // the climate gate must hold on its own (not the automated-run suppression)
		}
		Runner->EndFrameHandle = FCoreDelegates::OnEndFrame.AddStatic(&OnEndFrame);
		Runner->PostLoadHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddStatic(&OnPostLoadMap);
		if (!FApp::CanEverRender())
		{
			Check(false, TEXT("rendered run (the frames are read back from the viewport)"));
			Finish();
			return;
		}
		OpenNext();
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.StartFlickerSmoke"),
		TEXT("Dev check (section 19, rendered): opens each map (default the lake, the factory, L_MovementTest, L_WeatherRainTest) and measures its first 180 frames: no mean-luma jump between frames, no lightning outside a temperate Storm (also an arctic Blizzard with Codex.Env.Lightning 1); logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
