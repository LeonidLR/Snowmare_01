// Dev-only console command: the RENDERED check of "no falling snow indoors" (user report 2026-10-23: «I went into a building to open a chest
// and the snow was falling INSIDE the building too»; docs/systems/environment-weather.md section 12.b). Needs a real RHI (the snow FX never
// spawns under -nullrhi): Scripts/smoke_parallel.ps1 runs it offscreen on the lake because Scripts/test_map.json gives it "rendered": true.
// By hand:
//   UnrealEditor-Cmd.exe CodexTactics.uproject /Game/Maps/L_Demo01_A_FrozenLake -LevelJson=L_Demo01_A_FrozenLake.json -game -RenderOffscreen
//     -ResX=1280 -ResY=720 -nosound -UseFixedTimeStep -FPS=60 -ExecCmds="CodexTactics.SnowIndoorSmoke"
// The fishermen's hut (the Shelter camera zone Trigger_BoatsCabin, else the nearest Closed / Shelter zone to the squad), Blizzard 12:00, the
// squad leader teleported inside and frozen. A camera under the hut's roof looks straight down at the floor (everything it sees lies inside the
// hut, below the roof); the snow's near-camera fade is switched off for the run (a dev tuning override) so a flake 2 m away would show.
// Pixel A / B: the frame with the snow Niagara system visible vs hidden one frame pair later (TAA / eye adaptation / motion blur off); pixels
// brighter by > 16 / 255 with the snow = snow pixels (summed over 6 samples); the same compare of two hidden frames = the noise floor.
//   1. control - Codex.Env.SnowIndoorKill 0 (the bug): flakes inside the hut (many snow pixels) - proves the probe sees flakes;
//   2. fix - Codex.Env.SnowIndoorKill 1: the component lists a kill box that holds the leader, and no snow pixels inside;
//   3. outside - the same camera 4.5 m past the zone's side, fix on: flakes there (the box is not oversized).
// Stills: Saved/Screenshots/Environment/snowfx2/hut_inside_killoff.png, hut_inside_killon.png, hut_outside_killon.png.
// Prints "[SnowIndoorSmoke] PASS/FAIL ..." and "Smoke RESULT: PASS|FAIL", then exits.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/CameraZoneVolume.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "CollisionQueryParams.h"
#include "Combat/HealthComponent.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Environment/EnvironmentSubsystem.h"
#include "Environment/WeatherPresenterActor.h"
#include "Environment/WeatherSnowFxComponent.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace SnowIndoorSmoke
{
	constexpr int32 ReadyTimeoutFrames = 60 * 60;
	constexpr int32 SettleFrames = 60 * 9;      // the blizzard fades in (0.35 / s) and the flakes reach the view
	constexpr int32 PhaseFrames = 60 * 4;       // after a CVar / camera change: the flakes in flight die or blow in
	constexpr int32 GapFrames = 4;              // frames between toggling the Niagara visibility and reading the viewport
	constexpr int32 Samples = 6;
	constexpr int32 SampleSpacing = 20;
	constexpr int32 BrightDelta = 16;
	constexpr double CameraFovDeg = 90.;
	constexpr double OutsideCm = 450.;

	enum class EStage : uint8 { WaitReady, Settle, Phase, Sample, Done };

	struct FPhase
	{
		const TCHAR* Name;
		bool bKill;
		bool bOutside;
	};

	const FPhase Phases[] = {
		{ TEXT("inside_killoff"), false, false },
		{ TEXT("inside_killon"), true, false },
		{ TEXT("outside_killon"), true, true },
	};

	struct FResult
	{
		int32 Snow = 0;   // summed over the samples
		int32 Noise = 0;  // summed over the samples
	};

	struct FState
	{
		EStage Stage = EStage::WaitReady;
		int32 Frame = 0;
		int32 StageFrame = 0;
		int32 PhaseIndex = 0;
		int32 SampleIndex = 0;
		int32 SampleStep = 0;
		int32 Failures = 0;
		TWeakObjectPtr<ACameraZoneVolume> Hut;
		TWeakObjectPtr<ACameraActor> Camera;
		TWeakObjectPtr<UWeatherSnowFxComponent> SnowFx;
		FVector InsideEye = FVector::ZeroVector;
		FVector OutsideEye = FVector::ZeroVector;
		FVector LeaderSpot = FVector::ZeroVector;
		TArray<FColor> A;
		TArray<FColor> B;
		FResult Results[UE_ARRAY_COUNT(Phases)];
		TArray<FBox> KillBoxesSeen;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[SnowIndoorSmoke] %s: %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	void SetCVar(const TCHAR* Name, const TCHAR* Value)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Variable->Set(Value, ECVF_SetByConsole);
		}
	}

	FString ShotDir()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir() / TEXT("..") / TEXT("Environment") / TEXT("snowfx2"));
	}

	/**
	 * The frame goes through the engine's screenshot pipeline (the path the stills use; a ReadPixels from a ticker reads a black offscreen
	 * viewport): UGameViewportClient::OnScreenshotCaptured gets the pixels instead of a file being written.
	 */
	struct FCapture
	{
		TArray<FColor> Pixels;
		FIntPoint Size = FIntPoint::ZeroValue;
		bool bPending = false;
		bool bDone = false;
		FDelegateHandle Handle;
	};

	FCapture GCapture;

	void OnCaptured(int32 Width, int32 Height, const TArray<FColor>& Pixels)
	{
		if (!GCapture.bPending)
		{
			return;
		}
		GCapture.Pixels = Pixels;
		GCapture.Size = FIntPoint(Width, Height);
		GCapture.bPending = false;
		GCapture.bDone = true;
	}

	void RequestCapture()
	{
		if (!GCapture.Handle.IsValid())
		{
			GCapture.Handle = UGameViewportClient::OnScreenshotCaptured().AddStatic(&OnCaptured);
		}
		GCapture.bPending = true;
		GCapture.bDone = false;
		FScreenshotRequest::RequestScreenshot(/*bInShowUI*/ false);
	}

	/** True once the requested frame arrived (moved into Out). */
	bool TakeCapture(TArray<FColor>& Out, FIntPoint& OutSize)
	{
		if (!GCapture.bDone)
		{
			return false;
		}
		Out = MoveTemp(GCapture.Pixels);
		OutSize = GCapture.Size;
		GCapture.bDone = false;
		return true;
	}

	void SavePng(const TArray<FColor>& Pixels, const FIntPoint& Size, const FString& Path)
	{
		if (Pixels.Num() != Size.X * Size.Y || Size.X <= 0)
		{
			return;
		}
		TArray<FColor> Opaque = Pixels;
		for (FColor& Pixel : Opaque)
		{
			Pixel.A = 255;
		}
		TArray64<uint8> Compressed;
		FImageUtils::PNGCompressImageArray(Size.X, Size.Y, TArrayView64<const FColor>(Opaque.GetData(), Opaque.Num()), Compressed);
		FFileHelper::SaveArrayToFile(Compressed, *Path);
	}

	/** Pixels of With brighter than Without by more than BrightDelta (luma, 0..255). */
	int32 CountBrighter(const TArray<FColor>& With, const TArray<FColor>& Without)
	{
		if (With.Num() != Without.Num())
		{
			return -1;
		}
		int32 Count = 0;
		for (int32 Index = 0; Index < With.Num(); ++Index)
		{
			const FColor& P = With[Index];
			const FColor& Q = Without[Index];
			const int32 Lp = (P.R * 54 + P.G * 183 + P.B * 19) >> 8;
			const int32 Lq = (Q.R * 54 + Q.G * 183 + Q.B * 19) >> 8;
			Count += Lp - Lq > BrightDelta ? 1 : 0;
		}
		return Count;
	}

	/** The first visible surface below From (the squad ignored; the hut's floor only blocks the visibility channel), else From - Down. */
	FVector Floor(UWorld& World, const FVector& From, double Down)
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(SnowIndoorSmokeFloor), true);
		if (USquadSubsystem* Squad = World.GetSubsystem<USquadSubsystem>())
		{
			for (AOperativeCharacter* Each : Squad->GetMembers())
			{
				Params.AddIgnoredActor(Each);
			}
		}
		return World.LineTraceSingleByChannel(Hit, From, From - FVector(0., 0., Down), ECC_Visibility, Params) ? Hit.ImpactPoint : From - FVector(0., 0., Down);
	}

	/** The hut: the zone named *BoatsCabin*, else the Closed / Shelter zone nearest to the leader. */
	ACameraZoneVolume* FindHut(UWorld& World, const FVector& Near)
	{
		ACameraZoneVolume* Best = nullptr;
		double BestDistance = TNumericLimits<double>::Max();
		for (TActorIterator<ACameraZoneVolume> It(&World); It; ++It)
		{
			if (It->Environment != ECameraZoneEnvironment::Closed && It->Environment != ECameraZoneEnvironment::Shelter)
			{
				continue;
			}
			if (It->GetActorNameOrLabel().Contains(TEXT("BoatsCabin")))
			{
				return *It;
			}
			const double Distance = FVector::DistSquared2D(It->GetActorLocation(), Near);
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				Best = *It;
			}
		}
		return Best;
	}

	void KeepSquadSafe(UWorld& World)
	{
		USquadSubsystem* Squad = World.GetSubsystem<USquadSubsystem>();
		for (AOperativeCharacter* Each : Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>())
		{
			Each->HealthComponent->SetMaxHealth(100000.f, false);
			Each->HealthComponent->Heal(100000.f);
			Each->bTacticalCeaseFire = true;
			Each->ColdLevel = 0.f;
		}
	}

	void View(UWorld& World, FState& State, const FVector& Eye)
	{
		APlayerController* PC = World.GetFirstPlayerController();
		if (!PC)
		{
			return;
		}
		if (AHUD* Hud = PC->GetHUD())
		{
			Hud->bShowHUD = false;
		}
		if (!State.Camera.IsValid())
		{
			State.Camera = World.SpawnActor<ACameraActor>(Eye, FRotator(-90., 0., 0.));
		}
		if (ACameraActor* Camera = State.Camera.Get())
		{
			Camera->SetActorLocationAndRotation(Eye, FRotator(-90., 0., 0.));
			Camera->GetCameraComponent()->SetFieldOfView(CameraFovDeg);
			Camera->GetCameraComponent()->bConstrainAspectRatio = false;
			Camera->GetCameraComponent()->PostProcessSettings.bOverride_MotionBlurAmount = true;
			Camera->GetCameraComponent()->PostProcessSettings.MotionBlurAmount = 0.f;
			if (PC->GetViewTarget() != Camera)
			{
				PC->SetViewTarget(Camera);
			}
		}
	}

	void Finish(FState& State)
	{
		UGameViewportClient::OnScreenshotCaptured().Remove(GCapture.Handle);
		GCapture.Handle.Reset();
		SetCVar(TEXT("Codex.Env.SnowIndoorKill"), TEXT("1"));
		if (UWeatherSnowFxComponent* SnowFx = State.SnowFx.Get())
		{
			SnowFx->SetTuningOverride(TOptional<FWeatherSnowFxTuning>());
		}
		const bool bPass = State.Failures == 0;
		UE_LOG(LogCodexTactics, Display, TEXT("[SnowIndoorSmoke] RESULT %s (%d failed checks)"), bPass ? TEXT("PASS") : TEXT("FAIL"), State.Failures);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), bPass ? TEXT("PASS") : TEXT("FAIL"));
		State.Stage = EStage::Done;
		FPlatformMisc::RequestExit(false, TEXT("SnowIndoorSmoke"));
	}

	void Evaluate(FState& State)
	{
		const FResult& Off = State.Results[0];
		const FResult& On = State.Results[1];
		const FResult& Outside = State.Results[2];
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Phases); ++Index)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[SnowIndoorSmoke] %s: snow pixels %d, noise pixels %d"), Phases[Index].Name, State.Results[Index].Snow,
				State.Results[Index].Noise);
		}
		const int32 NoiseFloor = FMath::Max3(Off.Noise, On.Noise, Outside.Noise);
		const int32 SeenThreshold = FMath::Max(60, NoiseFloor * 4 + 20);
		const int32 CleanThreshold = FMath::Max(15, NoiseFloor * 2 + 10);
		Check(State, Off.Snow >= SeenThreshold, FString::Printf(TEXT("control (kill off): flakes inside the hut seen - %d snow pixels (>= %d)"), Off.Snow, SeenThreshold));
		Check(State, On.Snow <= CleanThreshold, FString::Printf(TEXT("fix (kill on): no flakes inside the hut - %d snow pixels (<= %d)"), On.Snow, CleanThreshold));
		Check(State, Outside.Snow >= SeenThreshold, FString::Printf(TEXT("outside the hut (kill on): flakes still fall %.1f m past the zone - %d snow pixels (>= %d)"),
			OutsideCm / 100., Outside.Snow, SeenThreshold));
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		UWorld* World = WeakWorld.Get();
		if (!World || State.Stage == EStage::Done)
		{
			return false;
		}
		++State.Frame;
		++State.StageFrame;
		UEnvironmentSubsystem* Environment = UEnvironmentSubsystem::Get(World);
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		AWeatherPresenterActor* Presenter = AWeatherPresenterActor::Find(World);
		UWeatherSnowFxComponent* SnowFx = Presenter ? Presenter->GetSnowFx() : nullptr;

		switch (State.Stage)
		{
		case EStage::WaitReady:
		{
			if (!FApp::CanEverRender())
			{
				Check(State, false, TEXT("needs a renderer (run it with -RenderOffscreen; test_map.json \"rendered\": true)"));
				Finish(State);
				return false;
			}
			if (State.Frame > ReadyTimeoutFrames)
			{
				Check(State, false, TEXT("the level, the environment, the presenter and the squad leader got ready"));
				Finish(State);
				return false;
			}
			if (!Environment || !Environment->IsInitialised() || !Leader || !Presenter || !Presenter->IsApplying() || !SnowFx || State.Frame < 90)
			{
				return true;
			}
			ACameraZoneVolume* Hut = FindHut(*World, Leader->GetActorLocation());
			Check(State, Hut != nullptr, FString::Printf(TEXT("the hut zone found (%s)"), Hut ? *Hut->GetActorNameOrLabel() : TEXT("none")));
			if (!Hut)
			{
				Finish(State);
				return false;
			}
			State.Hut = Hut;
			State.SnowFx = SnowFx;
			const FTransform Zone = Hut->GetZoneTransform();
			const FVector Half = Hut->GetZoneHalfExtent();
			const FVector Centre = Zone.GetLocation();
			const double TopZ = Centre.Z + Half.Z;
			// The floor below the zone's middle height (a trace from near the top would stop on the ceiling).
			const FVector FloorPoint = Floor(*World, FVector(Centre.X, Centre.Y, Centre.Z), Half.Z + 300.);
			// The ceiling straight above the floor (else the zone's top).
			FHitResult Hit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(SnowIndoorSmokeRoof), true, Leader);
			const double RoofZ = World->LineTraceSingleByChannel(Hit, FloorPoint + FVector(0., 0., 60.), FloorPoint + FVector(0., 0., 1500.), ECC_Visibility, Params)
				? Hit.ImpactPoint.Z : TopZ;
			const double EyeZ = FMath::Min(RoofZ - 30., FloorPoint.Z + 260.);
			// The leader stands in a corner (out of the probe's view), frozen; the probe looks down at the middle of the floor.
			const FVector Corner = Zone.TransformPosition(FVector(-Half.X * 0.6, -Half.Y * 0.55, 0.));
			State.LeaderSpot = Floor(*World, FVector(Corner.X, Corner.Y, Centre.Z), Half.Z + 300.) + FVector(0., 0., 95.);
			State.InsideEye = FVector(FloorPoint.X, FloorPoint.Y, EyeZ);
			// Outside: the side (+-X / +-Y, OutsideCm past the zone) whose ground is nearest the hut's floor level.
			double BestDz = TNumericLimits<double>::Max();
			for (const FVector& Dir : { FVector(0., -1., 0.), FVector(0., 1., 0.), FVector(-1., 0., 0.), FVector(1., 0., 0.) })
			{
				const FVector Local(Dir.X * (Half.X + OutsideCm), Dir.Y * (Half.Y + OutsideCm), 0.);
				const FVector Side = Zone.TransformPosition(Local);
				const FVector Ground = Floor(*World, FVector(Side.X, Side.Y, FloorPoint.Z + 400.), 1200.);
				const double Dz = FMath::Abs(Ground.Z - FloorPoint.Z);
				if (Dz < BestDz)
				{
					BestDz = Dz;
					State.OutsideEye = Ground + FVector(0., 0., EyeZ - FloorPoint.Z);
				}
			}
			Leader->SetActorLocation(State.LeaderSpot, false, nullptr, ETeleportType::TeleportPhysics);
			UE_LOG(LogCodexTactics, Display, TEXT("[SnowIndoorSmoke] hut %s (%s) centre %s half %s; floor z %.0f, roof z %.0f, eye z %.0f; outside eye %s"),
				*Hut->GetActorNameOrLabel(), Hut->Environment == ECameraZoneEnvironment::Closed ? TEXT("Closed") : TEXT("Shelter"), *Centre.ToCompactString(),
				*Half.ToCompactString(), FloorPoint.Z, RoofZ, EyeZ, *State.OutsideEye.ToCompactString());
			Environment->SetClockScale(0.f);
			Environment->ForceWeather(EWeatherState::Blizzard, true);
			Environment->SetHour(12.f);
			// A flake 1-2 m from the probe must show: no near-camera fade for the run.
			FWeatherSnowFxTuning Tuning = SnowFx->GetActiveTuning();
			Check(State, SnowFx->IsUsingSettingsAsset(), TEXT("the snow look comes from DA_SnowFX"));
			Tuning.NearFadeStart = 0.f;
			Tuning.NearFadeLength = 1.f;
			SnowFx->SetTuningOverride(Tuning);
			for (const TCHAR* Command : { TEXT("r.AntiAliasingMethod 0"), TEXT("r.MotionBlurQuality 0"), TEXT("r.EyeAdaptationQuality 0") })
			{
				GEngine->Exec(World, Command);
			}
			IFileManager::Get().MakeDirectory(*ShotDir(), true);
			State.Stage = EStage::Settle;
			State.StageFrame = 0;
			return true;
		}

		case EStage::Settle:
		case EStage::Phase:
		case EStage::Sample:
			break;

		default:
			return false;
		}

		// Every frame from here: the squad safe and frozen, the probe camera in place.
		KeepSquadSafe(*World);
		if (Leader && FVector::DistSquared(Leader->GetActorLocation(), State.LeaderSpot) > FMath::Square(60.))
		{
			Leader->SetActorLocation(State.LeaderSpot, false, nullptr, ETeleportType::TeleportPhysics);
		}
		for (AOperativeCharacter* Each : Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>())
		{
			Each->CustomTimeDilation = 0.f;
		}
		const FPhase& Phase = Phases[FMath::Min(State.PhaseIndex, static_cast<int32>(UE_ARRAY_COUNT(Phases)) - 1)];
		View(*World, State, Phase.bOutside ? State.OutsideEye : State.InsideEye);

		if (State.Stage == EStage::Settle)
		{
			if (!SnowFx || !SnowFx->IsSnowActive())
			{
				State.StageFrame = FMath::Min(State.StageFrame, 1);
				if (State.Frame > ReadyTimeoutFrames * 2)
				{
					Check(State, false, TEXT("the falling snow became active in the blizzard"));
					Finish(State);
					return false;
				}
				return true;
			}
			if (State.StageFrame >= SettleFrames)
			{
				State.Stage = EStage::Phase;
				State.StageFrame = 0;
				SetCVar(TEXT("Codex.Env.SnowIndoorKill"), Phase.bKill ? TEXT("1") : TEXT("0"));
			}
			return true;
		}

		if (State.Stage == EStage::Phase)
		{
			if (State.StageFrame >= PhaseFrames)
			{
				if (Phase.bKill)
				{
					// The fix holds the hut: one of the boxes the component wrote contains the leader.
					const TArray<FBox>& Boxes = SnowFx->GetKillBoxes();
					const bool bHolds = Boxes.ContainsByPredicate([&State](const FBox& Box) { return WeatherSnowFxRules::IsInsideKillBox(State.LeaderSpot, Box); });
					Check(State, bHolds, FString::Printf(TEXT("%s: a kill box (of %d) holds the leader in the hut"), Phase.Name, Boxes.Num()));
				}
				State.Stage = EStage::Sample;
				State.StageFrame = 0;
				State.SampleIndex = 0;
				State.SampleStep = 0;
			}
			return true;
		}

		// Sample: visible -> frame A; hide -> frame B; frame B2 (noise); show again. Each frame is requested GapFrames after the toggle and
		// waited for.
		if (!SnowFx)
		{
			return true;
		}
		FIntPoint Size;
		FResult& Result = State.Results[State.PhaseIndex];
		const int32 Local = State.StageFrame;
		if (Local == 1)
		{
			SnowFx->SetHiddenForCapture(false);
			State.SampleStep = 0;
		}
		else if (State.SampleStep == 0 && Local >= 1 + GapFrames)
		{
			RequestCapture();
			State.SampleStep = 1;
		}
		else if (State.SampleStep == 1 && TakeCapture(State.A, Size))
		{
			SnowFx->SetHiddenForCapture(true);
			State.SampleStep = 2;
			State.StageFrame = 100;
		}
		else if (State.SampleStep == 2 && Local >= 100 + GapFrames)
		{
			RequestCapture();
			State.SampleStep = 3;
		}
		else if (State.SampleStep == 3 && TakeCapture(State.B, Size))
		{
			Result.Snow += FMath::Max(CountBrighter(State.A, State.B), 0);
			if (State.SampleIndex == 0)
			{
				SavePng(State.A, Size, ShotDir() / FString::Printf(TEXT("hut_%s.png"), Phase.Name));
			}
			State.SampleStep = 4;
			State.StageFrame = 200;
		}
		else if (State.SampleStep == 4 && Local >= 200 + GapFrames)
		{
			RequestCapture();
			State.SampleStep = 5;
		}
		else if (State.SampleStep == 5)
		{
			TArray<FColor> B2;
			if (TakeCapture(B2, Size))
			{
				Result.Noise += FMath::Max(CountBrighter(B2, State.B), 0);
				SnowFx->SetHiddenForCapture(false);
				State.SampleStep = 6;
				State.StageFrame = 300;
			}
		}
		else if (State.SampleStep == 6 && Local >= 300 + SampleSpacing)
		{
			++State.SampleIndex;
			State.StageFrame = 0;
			if (State.SampleIndex >= Samples)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("[SnowIndoorSmoke] %s sampled: snow %d, noise %d"), Phase.Name, Result.Snow, Result.Noise);
				++State.PhaseIndex;
				if (State.PhaseIndex >= static_cast<int32>(UE_ARRAY_COUNT(Phases)))
				{
					Evaluate(State);
					Finish(State);
					return false;
				}
				const FPhase& Next = Phases[State.PhaseIndex];
				SetCVar(TEXT("Codex.Env.SnowIndoorKill"), Next.bKill ? TEXT("1") : TEXT("0"));
				State.Stage = EStage::Phase;
			}
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
		// Every frame (the pixel A / B is frame exact).
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), 0.f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.SnowIndoorSmoke"),
		TEXT("Dev check (RENDERED, the lake): no falling snow inside the fishermen's hut in a blizzard - a pixel A/B (snow visible / hidden) under the ")
		TEXT("hut's roof with the indoor kill off (control: flakes), on (none) and outside the hut (flakes). Logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
