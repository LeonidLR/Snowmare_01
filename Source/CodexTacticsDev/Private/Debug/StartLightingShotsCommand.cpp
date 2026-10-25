// Dev-only: rendered stills of the level START, frame by frame as the player sees it (docs/systems/environment-weather.md section 16, game
// designer 2026-10-25: "first a sunset shows, then it switches to night ... the snow also starts falling with a delay"). UE-only.
//   UnrealEditor.exe CodexTactics.uproject /Game/Maps/L_Demo01_A_FrozenLake -game -RenderOffscreen -ResX=1280 -ResY=720 -nosound
//     -LevelJson=L_Demo01_A_FrozenLake.json -StartLightingShots=night_lights
//   (an unlocked level takes -EnvHour=<h> -EnvWeather=<name>: the factory with -EnvHour=12 -EnvWeather=Clear is a day start; with
//   -StartLightingShots there is no -ExecCmds, so the environment is NOT forced to the automated neutral start)
// -StartLightingShots=<tag>: armed when the map has loaded (FCoreUObjectDelegates::PostLoadMapWithWorld, after BeginPlay, before the first
// draw), so frame 1 is the level's FIRST rendered frame. The console command CodexTactics.StartLightingShots <tag> does the same from the
// frame it runs in (-ExecCmds run after the first frame was drawn, so its frame 1 is the level's 2nd-3rd).
// Captures frames 1, 2, 3, 4, 5 and 60 into Saved/Screenshots/StartLighting/<map>_<tag>_fNNN.png, no camera / HUD change. One
// "[StartShots]" log line per frame: game time, the presenter (applying, applications, rig mode, the sun / moon state), the falling snow
// (active, warm-ups, intensity, live particles per emitter), shaders still compiling. Then exits.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "CodexTactics.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/DirectionalLightComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Environment/EnvironmentStartRules.h"
#include "Environment/EnvironmentSubsystem.h"
#include "Environment/WeatherPresenterActor.h"
#include "Environment/WeatherSnowFxComponent.h"
#include "Debug/FrontendSmokeUtils.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UObject/UObjectGlobals.h"
#include "UnrealClient.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

namespace StartLightingShots
{
	const int32 CaptureFrames[] = { 1, 2, 3, 4, 5, 60, 240 };

	struct FRunner
	{
		int32 Frame = 1;
		FString Dir;
		FString Tag;
		FDelegateHandle EndFrameHandle;
	};

	TSharedPtr<FRunner> Runner;

	int32 ShadersPending()
	{
#if WITH_EDITOR
		return GShaderCompilingManager ? GShaderCompilingManager->GetNumRemainingJobs() : 0;
#else
		return 0;
#endif
	}

	void Capture(UWorld* World, int32 Frame)
	{
		FString Map = World ? World->GetMapName() : FString(TEXT("none"));
		if (World)
		{
			Map.RemoveFromStart(World->StreamingLevelsPrefix);
		}
		const FString Path = Runner->Dir / FString::Printf(TEXT("%s_%s_f%03d.png"), *Map, *Runner->Tag, Frame);
		FScreenshotRequest::RequestScreenshot(Path, /*bInShowUI*/ true, /*bAddFilenameSuffix*/ false);
		const AWeatherPresenterActor* Presenter = World ? AWeatherPresenterActor::Find(World) : nullptr;
		const UEnvironmentSubsystem* Env = World ? UEnvironmentSubsystem::Get(World) : nullptr;
		const UDirectionalLightComponent* Sun = Presenter ? Presenter->GetSunLight() : nullptr;
		const UDirectionalLightComponent* Moon = Presenter ? Presenter->GetActiveMoon() : nullptr;
		const UWeatherSnowFxComponent* Snow = Presenter ? Presenter->GetSnowFx() : nullptr;
		const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		const APlayerCameraManager* CameraManager = PC ? PC->PlayerCameraManager.Get() : nullptr;
		UE_LOG(LogCodexTactics, Display, TEXT("[StartShots] camera fade: enabled %d amount %.2f colour %s, camera %s"),
			CameraManager && CameraManager->bEnableFading ? 1 : 0, CameraManager ? CameraManager->FadeAmount : -1.f,
			CameraManager ? *CameraManager->FadeColor.ToString() : TEXT("-"), CameraManager ? *CameraManager->GetCameraLocation().ToCompactString() : TEXT("-"));
		UE_LOG(LogCodexTactics, Display,
			TEXT("[StartShots] frame %d (engine frame %llu, game %.3f s) -> %s | presenter %s applying %d applications %d rig %s, sun visible %d pitch %.1f %.2f lux, moon visible %d %.2f, exposure snap %d, start cover %d | snow active %d warm-ups %d (%.1f s) intensity %.2f/%.2f particles %s | shaders pending %d | %s"),
			Frame, static_cast<uint64>(GFrameCounter), World ? World->GetTimeSeconds() : -1.f, *FPaths::GetCleanFilename(Path), Presenter ? TEXT("yes") : TEXT("no"),
			Presenter && Presenter->IsApplying() ? 1 : 0, Presenter ? Presenter->GetApplyCount() : 0,
			Presenter ? *EnvironmentStartRules::RigModeToString(Presenter->GetRigMode()) : TEXT("-"), Sun && Sun->IsVisible() ? 1 : 0,
			Sun ? Sun->GetComponentRotation().Pitch : 0.f, Sun ? Sun->Intensity : 0.f, Moon && Moon->IsVisible() ? 1 : 0, Moon ? Moon->Intensity : 0.f,
			Presenter && Presenter->IsExposureSnapping() ? 1 : 0, Presenter && Presenter->IsStartCoverOn() ? 1 : 0, Snow && Snow->IsSnowActive() ? 1 : 0,
			Snow ? Snow->GetWarmupCount() : 0,
			Snow ? Snow->GetLastWarmupSeconds() : 0.f, Snow ? Snow->GetCurrentIntensity() : 0.f, Snow ? Snow->GetTargetIntensity() : 0.f,
			Snow ? *Snow->DescribeParticles() : TEXT("-"), ShadersPending(),
			Env ? *Env->Describe() : TEXT("no environment"));
	}

	void Finish()
	{
		if (Runner.IsValid())
		{
			FCoreDelegates::OnEndFrame.Remove(Runner->EndFrameHandle);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("[StartShots] done -> %s"), Runner.IsValid() ? *Runner->Dir : TEXT("?"));
		Runner.Reset();
		FPlatformMisc::RequestExit(false, TEXT("StartLightingShots"));
	}

	void OnEndFrame()
	{
		if (!Runner.IsValid())
		{
			return;
		}
		// The frame that just ended is Runner->Frame; the next one is captured when it is in the list (the request is served in its draw).
		const int32 Next = ++Runner->Frame;
		const int32 Last = CaptureFrames[UE_ARRAY_COUNT(CaptureFrames) - 1];
		if (Next > Last + 3)
		{
			Finish();
			return;
		}
		for (const int32 CaptureFrame : CaptureFrames)
		{
			if (CaptureFrame == Next)
			{
				Capture(FrontendSmokeUtils::FindGameWorld(), Next);
			}
		}
	}

	void Start(const FString& Tag, UWorld* World)
	{
		Runner = MakeShared<FRunner>();
		Runner->Tag = Tag.IsEmpty() ? FString(TEXT("start")) : Tag;
		FString Dir = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir() / TEXT("..") / TEXT("StartLighting"));
		FPaths::CollapseRelativeDirectories(Dir);
		IFileManager::Get().MakeDirectory(*Dir, true);
		Runner->Dir = Dir;
		// Frame 1 = the next draw (served by the viewport in this engine frame's draw or, from PostLoadMapWithWorld, the first one).
		Capture(World, 1);
		Runner->EndFrameHandle = FCoreDelegates::OnEndFrame.AddStatic(&OnEndFrame);
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		Start(Args.Num() > 0 ? Args[0] : FString(), World);
	}

	/** -StartLightingShots=<tag>: armed once, by the first game map that finished loading (before its first rendered frame). */
	void OnPostLoadMap(UWorld* World)
	{
		static bool bStarted = false;
		FString Tag;
		if (bStarted || !World || !World->IsGameWorld() || !FParse::Value(FCommandLine::Get(), TEXT("-StartLightingShots="), Tag))
		{
			return;
		}
		bStarted = true;
		Start(Tag, World);
	}

	struct FAutoStart
	{
		FAutoStart()
		{
			if (FString(FCommandLine::Get()).Contains(TEXT("-StartLightingShots=")))
			{
				FCoreUObjectDelegates::PostLoadMapWithWorld.AddStatic(&OnPostLoadMap);
			}
		}
	};
	static FAutoStart AutoStart;

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.StartLightingShots"),
		TEXT("Dev: rendered stills of frames 1-5 and 60 of the level start into Saved/Screenshots/StartLighting (<map>_<tag>_fNNN.png) + a log line per frame; needs rendering, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
