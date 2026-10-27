// Dev-only: EVERY frame of the level start, measured (docs/systems/environment-weather.md section 19, the start flicker; user report
// 2026-10-27 "a strange flicker when the level starts ... as if there's a thunderstorm"). UE-only.
//   UnrealEditor.exe CodexTactics.uproject /Game/Maps/L_Demo01_A_FrozenLake -game -RenderOffscreen -ResX=1280 -ResY=720 -nosound
//     -UseFixedTimeStep -FPS=60 -LevelJson=L_Demo01_A_FrozenLake.json -StartFlickerShots=before [-StartFlickerFrames=240]
//     [-LogCmds="LogCodexEnvTrace Verbose"]
// Armed when the map has loaded (FCoreUObjectDelegates::PostLoadMapWithWorld, before the first draw), so frame 1 is the level's first
// rendered frame; no -ExecCmds, so the environment is NOT forced to the automated neutral start (the player's start).
// The console command CodexTactics.StartFlickerShots <tag> [frames] does the same from the frame it runs in (e.g. in PIE).
// Output: Saved/Screenshots/Environment/flicker/<tag>/<map>.csv (StartFlickerProbe columns) + <map>/<map>_fNNN.png thumbnails (320 px),
// one "[StartFlicker]" Display line per frame (mean, diff, exposure, fade, the presenter / flash state) and a summary (the largest
// frame-to-frame mean step outside camera fades). Scripts/Tools/flicker_strip.py draws the luminance strips from the CSVs.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "CodexTactics.h"
#include "Debug/StartFlickerProbe.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UObject/UObjectGlobals.h"
#if WITH_EDITOR
#include "Containers/Ticker.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "FileHelpers.h"
#include "LevelEditor.h"
#include "Modules/ModuleManager.h"
#include "PlayInEditorDataTypes.h"
#endif

namespace StartFlickerShots
{
	struct FRunner
	{
		StartFlickerProbe::FRecorder Recorder;
		FString Dir;
		FString Map;
		FDelegateHandle EndFrameHandle;
		int32 LoggedFrames = 0;
		bool bExitWhenDone = true;
	};

	TSharedPtr<FRunner> Runner;

	void LogNewFrames()
	{
		const TArray<StartFlickerProbe::FFrameStats>& Stats = Runner->Recorder.GetStats();
		for (; Runner->LoggedFrames < Stats.Num(); ++Runner->LoggedFrames)
		{
			const StartFlickerProbe::FFrameStats& F = Stats[Runner->LoggedFrames];
			UE_LOG(LogCodexTactics, Display,
				TEXT("[StartFlicker] frame %d (engine %llu, t %.3f): mean %.2f (top %.1f mid %.1f bottom %.1f hud %.1f) diff %.2f, exposure %.4f lum %.4f, fade %.2f, snap %d, applies %d, sun %.2f moon %.2f sky %.2f, flash %.2f strikes %d"),
				F.Frame, F.EngineFrame, F.GameTime, F.Mean, F.Band[0], F.Band[1], F.Band[2], F.Band[3], F.Diff, F.Exposure, F.SceneLuminance, F.FadeAmount,
				F.bExposureSnap ? 1 : 0, F.ApplyCount, F.SunLux, F.MoonLux, F.SkyLight, F.FlashScale, F.Strikes);
		}
	}

	void Finish()
	{
		if (!Runner.IsValid())
		{
			return;
		}
		FCoreDelegates::OnEndFrame.Remove(Runner->EndFrameHandle);
		LogNewFrames();
		const FString Csv = Runner->Dir / (Runner->Map + TEXT(".csv"));
		Runner->Recorder.WriteCsv(Csv);
		int32 StepFrame = 0;
		const float Step = Runner->Recorder.MaxMeanStep(1, Runner->Recorder.GetStats().Num(), StepFrame);
		float MaxDiff = 0.f;
		int32 DiffFrame = 0;
		for (const StartFlickerProbe::FFrameStats& F : Runner->Recorder.GetStats())
		{
			if (F.Diff > MaxDiff && F.FadeAmount <= 0.001f)
			{
				MaxDiff = F.Diff;
				DiffFrame = F.Frame;
			}
		}
		UE_LOG(LogCodexTactics, Display, TEXT("[StartFlicker] done %s: %d frames, largest mean step %.2f at frame %d, largest cell diff %.2f at frame %d -> %s"),
			*Runner->Map, Runner->Recorder.GetStats().Num(), Step, StepFrame, MaxDiff, DiffFrame, *Csv);
		Runner->Recorder.End();
		const bool bExit = Runner->bExitWhenDone;
		Runner.Reset();
		if (bExit)
		{
			FPlatformMisc::RequestExit(false, TEXT("StartFlickerShots"));
		}
	}

	void OnEndFrame()
	{
		if (!Runner.IsValid())
		{
			return;
		}
		LogNewFrames();
		if (Runner->Recorder.IsDone())
		{
			Finish();
			return;
		}
		Runner->Recorder.RequestNext();
	}

	void Start(const FString& Tag, int32 Frames, UWorld* World, bool bExitWhenDone)
	{
		Runner = MakeShared<FRunner>();
		Runner->bExitWhenDone = bExitWhenDone;
		FString Map = World ? World->GetMapName() : FString(TEXT("none"));
		if (World)
		{
			Map.RemoveFromStart(World->StreamingLevelsPrefix);
		}
		Runner->Map = Map;
		FString Dir = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir() / TEXT("..") / TEXT("Environment") / TEXT("flicker") / (Tag.IsEmpty() ? FString(TEXT("start")) : Tag));
		FPaths::CollapseRelativeDirectories(Dir);
		Runner->Dir = Dir;
		UE_LOG(LogCodexTactics, Display, TEXT("[StartFlicker] recording %d frames of %s into %s"), Frames, *Map, *Dir);
		Runner->Recorder.Begin(Frames, Dir / Map, Map);
		Runner->EndFrameHandle = FCoreDelegates::OnEndFrame.AddStatic(&OnEndFrame);
	}

	/** -StartFlickerShots=<tag>: armed once, by the first game map that finished loading (before its first rendered frame). */
	void OnPostLoadMap(UWorld* World)
	{
		static bool bStarted = false;
		FString Tag;
		if (bStarted || !World || !World->IsGameWorld() || !FParse::Value(FCommandLine::Get(), TEXT("-StartFlickerShots="), Tag))
		{
			return;
		}
		int32 Frames = 240;
		FParse::Value(FCommandLine::Get(), TEXT("-StartFlickerFrames="), Frames);
		bStarted = true;
		Start(Tag, Frames, World, true);
	}

	struct FAutoStart
	{
		FAutoStart()
		{
			if (FString(FCommandLine::Get()).Contains(TEXT("-StartFlickerShots=")))
			{
				FCoreUObjectDelegates::PostLoadMapWithWorld.AddStatic(&OnPostLoadMap);
			}
		}
	};
	static FAutoStart AutoStart;

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		const int32 Frames = Args.Num() > 1 ? FCString::Atoi(*Args[1]) : 240;
		// From the console (a running game / PIE): record, do not exit.
		Start(Args.Num() > 0 ? Args[0] : FString(), FMath::Max(1, Frames), World, false);
	}

#if WITH_EDITOR
	/**
	 * CodexTactics.PIEFlickerShots <map> <tag> [frames]: the user's case - in the EDITOR (not -game), loads <map>, starts PIE in the level
	 * viewport (as the toolbar Play does), records from the PIE world's first frame (armed at FEditorDelegates::PostPIEStarted, after its
	 * BeginPlay, before its first draw) and exits. Real frame times (PIE has no fixed step).
	 *   UnrealEditor.exe CodexTactics.uproject -RenderOffscreen -ResX=1280 -ResY=720 -unattended -nosplash -nosound
	 *     "-ExecCmds=CodexTactics.PIEFlickerShots /Game/Maps/L_Demo01_A_FrozenLake pie"
	 */
	struct FPIERequest
	{
		FString Tag;
		int32 Frames = 240;
		FDelegateHandle PostPIEHandle;
	};
	TSharedPtr<FPIERequest> PIERequest;

	void OnPostPIEStarted(const bool /*bSimulating*/)
	{
		if (!PIERequest.IsValid())
		{
			return;
		}
		FEditorDelegates::PostPIEStarted.Remove(PIERequest->PostPIEHandle);
		UWorld* World = StartFlickerProbe::FindPlayWorld();
		UE_LOG(LogCodexTactics, Display, TEXT("[StartFlicker] PIE started: %s"), *GetNameSafe(World));
		Start(PIERequest->Tag, PIERequest->Frames, World, true);
		PIERequest.Reset();
	}

	void RunPIE(const TArray<FString>& Args, UWorld* /*World*/)
	{
		if (!GEditor || Args.Num() < 1)
		{
			UE_LOG(LogCodexTactics, Error, TEXT("[StartFlicker] PIEFlickerShots needs the editor and a map"));
			return;
		}
		const FString Map = Args[0];
		PIERequest = MakeShared<FPIERequest>();
		PIERequest->Tag = Args.Num() > 1 ? Args[1] : FString(TEXT("pie"));
		PIERequest->Frames = Args.Num() > 2 ? FMath::Max(1, FCString::Atoi(*Args[2])) : 240;
		UE_LOG(LogCodexTactics, Display, TEXT("[StartFlicker] PIE: loading %s"), *Map);
		FEditorFileUtils::LoadMap(Map, /*LoadAsTemplate*/ false, /*bShowProgress*/ false);
		PIERequest->PostPIEHandle = FEditorDelegates::PostPIEStarted.AddStatic(&OnPostPIEStarted);
		// A few editor frames after the load (the viewport settles, the asset compile queue drains a little), then Play in the level viewport.
		TSharedRef<int32> FramesLeft = MakeShared<int32>(30);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([FramesLeft](float)
		{
			if (--(*FramesLeft) > 0)
			{
				return true;
			}
			FRequestPlaySessionParams Params;
			FLevelEditorModule& LevelEditor = FModuleManager::Get().LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
			Params.DestinationSlateViewport = LevelEditor.GetFirstActiveViewport();
			GEditor->RequestPlaySession(Params);
			UE_LOG(LogCodexTactics, Display, TEXT("[StartFlicker] PIE requested (level viewport %s)"), Params.DestinationSlateViewport.IsSet() ? TEXT("yes") : TEXT("no"));
			return false;
		}));
	}

	static FAutoConsoleCommandWithWorldAndArgs PIECommand(
		TEXT("CodexTactics.PIEFlickerShots"),
		TEXT("Dev (editor only): CodexTactics.PIEFlickerShots <map> <tag> [frames] loads the map, starts PIE in the level viewport, records every frame like StartFlickerShots from the PIE world's first frame, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunPIE));
#endif // WITH_EDITOR

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.StartFlickerShots"),
		TEXT("Dev: records the luminance of every rendered frame (mean, bands, frame-to-frame diff, exposure, fade, presenter / flash state) for <frames> (240) frames into Saved/Screenshots/Environment/flicker/<tag>/ (CSV + 320 px thumbnails); -StartFlickerShots=<tag> arms it before the level's first frame and exits after."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
