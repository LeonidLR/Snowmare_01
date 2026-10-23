// Dev-only RENDERED check of the holographic tutorial marker (game designer play-test 2026-10-23: "all the disc-with-a-pole markers must
// be HOLOGRAPHIC, ORANGE"; docs/systems/tutorial-engine.md "Markers"):
//   UnrealEditor.exe <uproject> /Game/Maps/L_Demo01_A_FrozenLake -game -RenderOffscreen -ResX=1280 -ResY=720 -nosound -benchmark -fps=30
//     -LevelJson=L_Demo01_A_FrozenLake.json -ExecCmds="CodexTactics.HoloMarkerShots"
// On the squad leader's ground two markers side by side: LEFT the old placeholder look (BasicShapeMaterial, amber;
// ATutorialMarkerActor::ApplyLegacyLookForComparison), RIGHT the hologram (MI_HoloMarker_Orange, the default of every marker) - one fixed
// tactical view (own camera, pitch -52), captured in Clear 13:00 and Snowfall 23:00 (the weather presenter's day and night lighting):
// Saved/Screenshots/HoloMarker/holo_day.png, holo_night.png (labels drawn in-engine). Checks (log "HoloMarkerShots ok / FAIL"): the new
// marker and EVERY tutorial marker of the level (placed + the guide beacon) wear M_HoloMarker on ring and pole, the old one does not.
// Cost: GPU ms with 8 hologram markers in view vs none, at night (log "[HoloMarkerPerf]"). Exits when done; "RESULT: PASS / FAIL".

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "CanvasItem.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Debug/DebugDrawService.h"
#include "Debug/FrontendSmokeUtils.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "Environment/EnvironmentRules.h"
#include "Environment/EnvironmentSubsystem.h"
#include "Environment/WeatherPresenterActor.h"
#include "EngineUtils.h"
#include "GameFramework/HUD.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "RHI.h"
#include "RHIStats.h"
#include "Tutorial/TutorialMarkerActor.h"
#include "Tutorial/TutorialSubsystem.h"
#include "UnrealClient.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

namespace HoloMarkerShots
{
	constexpr int32 WarmupFrames = 150;
	constexpr int32 SettleFrames = 90;
	constexpr int32 MeasureFrames = 60;
	/** Off / on measuring rounds (alternated: the machine's own load drifts). */
	constexpr int32 PerfRounds = 8;
	constexpr int32 MaxFrames = 30 * 180;
	constexpr int32 PerfMarkers = 8;
	constexpr float ViewPitch = -52.f;
	constexpr float ViewDistance = 1500.f;
	constexpr float ViewFov = 50.f;
	constexpr float MarkerSpacing = 230.f;

	struct FLook
	{
		EWeatherState Weather;
		float Hour;
		const TCHAR* File;
		const TCHAR* Label;
	};
	const FLook Looks[] = {
		{ EWeatherState::Clear, 13.f, TEXT("holo_day.png"), TEXT("DAY - Clear 13:00") },
		{ EWeatherState::Snowfall, 23.f, TEXT("holo_night.png"), TEXT("NIGHT - Snowfall 23:00 (the lake)") },
	};

	enum class EStep : uint8 { Warmup, Setup, Settle, Shot, Write, PerfOff, PerfOn, Done };

	struct FRunner
	{
		EStep Step = EStep::Warmup;
		int32 Frame = 0;
		int32 StepFrame = 0;
		int32 LookIndex = 0;
		int32 Failures = 0;
		FString Dir;
		FString Label;
		FDelegateHandle EndFrameHandle;
		FDelegateHandle DrawHandle;
		TWeakObjectPtr<ACameraActor> Camera;
		TWeakObjectPtr<ATutorialMarkerActor> OldMarker;
		TWeakObjectPtr<ATutorialMarkerActor> NewMarker;
		TArray<TWeakObjectPtr<ATutorialMarkerActor>> PerfSet;
		FVector Centre = FVector::ZeroVector;
		FVector Right = FVector::RightVector;
		float ViewYaw = 45.f;
		double GpuSum = 0.0;
		int32 Samples = 0;
		int32 PerfRound = 0;
		TArray<double> OffMs;
		TArray<double> OnMs;
	};

	TSharedPtr<FRunner> Runner;

	void Check(bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("HoloMarkerShots %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		Runner->Failures += bOk ? 0 : 1;
	}

	int32 ShadersPending()
	{
#if WITH_EDITOR
		return GShaderCompilingManager ? GShaderCompilingManager->GetNumRemainingJobs() : 0;
#else
		return 0;
#endif
	}

	FVector Ground(UWorld* World, const FVector& Point)
	{
		FHitResult Hit;
		FCollisionQueryParams Params(TEXT("HoloMarkerGround"), false);
		if (const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>())
		{
			for (const AOperativeCharacter* Member : Squad->GetMembers())
			{
				Params.AddIgnoredActor(Member);
			}
		}
		if (World->LineTraceSingleByChannel(Hit, Point + FVector(0.f, 0.f, 400.f), Point - FVector(0.f, 0.f, 1000.f), ECC_Visibility, Params))
		{
			return Hit.ImpactPoint + FVector(0.f, 0.f, 1.f);
		}
		return Point;
	}

	/** A marker of our own: not registered with the tutorial (it would hide a marker without a step), shown now. */
	ATutorialMarkerActor* SpawnMarker(UWorld* World, const FVector& At, bool bShow)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ATutorialMarkerActor* Marker = World->SpawnActor<ATutorialMarkerActor>(Ground(World, At), FRotator::ZeroRotator, Params);
		if (Marker)
		{
			if (UTutorialSubsystem* Tutorial = World->GetSubsystem<UTutorialSubsystem>())
			{
				Tutorial->UnregisterMarker(Marker);
			}
			Marker->SetMarkerShown(bShow);
		}
		return Marker;
	}

	void View(UWorld* World)
	{
		APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
		if (!PC)
		{
			return;
		}
		if (AHUD* Hud = PC->GetHUD())
		{
			Hud->bShowHUD = false;
		}
		const FRotator Rotation(ViewPitch, Runner->ViewYaw, 0.f);
		const FVector Eye = Runner->Centre + FVector(0.f, 0.f, 120.f) - Rotation.Vector() * ViewDistance;
		if (!Runner->Camera.IsValid())
		{
			Runner->Camera = World->SpawnActor<ACameraActor>(Eye, Rotation);
		}
		if (ACameraActor* Camera = Runner->Camera.Get())
		{
			Camera->SetActorLocationAndRotation(Eye, Rotation);
			Camera->GetCameraComponent()->SetFieldOfView(ViewFov);
			Camera->GetCameraComponent()->bConstrainAspectRatio = false;
			if (PC->GetViewTarget() != Camera)
			{
				PC->SetViewTarget(Camera);
			}
		}
	}

	void DrawLabel(UCanvas* Canvas, UFont* Font, const FString& Text, float X, float Y, float Scale, const FLinearColor& Color)
	{
		FCanvasTextItem Shadow(FVector2D(X + 2.f, Y + 2.f), FText::FromString(Text), Font, FLinearColor::Black);
		Shadow.Scale = FVector2D(Scale, Scale);
		Canvas->DrawItem(Shadow);
		FCanvasTextItem Item(FVector2D(X, Y), FText::FromString(Text), Font, Color);
		Item.Scale = FVector2D(Scale, Scale);
		Canvas->DrawItem(Item);
	}

	void OnDraw(UCanvas* Canvas, APlayerController* /*Controller*/)
	{
		if (!Runner.IsValid() || !Canvas || !GEngine || Runner->Label.IsEmpty())
		{
			return;
		}
		UFont* Font = GEngine->GetLargeFont();
		if (!Font)
		{
			return;
		}
		DrawLabel(Canvas, Font, Runner->Label, 18.f, 14.f, 1.2f, FLinearColor(1.f, 0.95f, 0.8f));
		auto Tag = [Canvas, Font](const ATutorialMarkerActor* Marker, const TCHAR* Text)
		{
			if (!Marker)
			{
				return;
			}
			const FVector Screen = Canvas->Project(Marker->GetActorLocation() - FVector(0.f, 0.f, 10.f));
			if (Screen.Z > 0.f)
			{
				float W = 0.f, H = 0.f;
				Canvas->TextSize(Font, Text, W, H, 1.f, 1.f);
				DrawLabel(Canvas, Font, Text, Screen.X - W * 0.5f, Screen.Y + 70.f, 1.f, FLinearColor::White);
			}
		};
		Tag(Runner->OldMarker.Get(), TEXT("OLD"));
		Tag(Runner->NewMarker.Get(), TEXT("NEW (hologram)"));
	}

	void CheckMaterials(UWorld* World)
	{
		const ATutorialMarkerActor* Old = Runner->OldMarker.Get();
		const ATutorialMarkerActor* New = Runner->NewMarker.Get();
		Check(New && New->IsHologram(), TEXT("the new marker wears M_HoloMarker (ring + pole)"));
		Check(Old && !Old->IsHologram(), TEXT("the comparison marker wears the old placeholder"));
		int32 Level = 0;
		int32 Holo = 0;
		for (TActorIterator<ATutorialMarkerActor> It(World); It; ++It)
		{
			if (*It == Old)
			{
				continue;
			}
			++Level;
			Holo += It->IsHologram() ? 1 : 0;
		}
		Check(Level > 0 && Holo == Level, FString::Printf(TEXT("every other tutorial marker of the level (placed + spawned) is a hologram: %d of %d"), Holo, Level));
	}

	void Finish()
	{
		UE_LOG(LogCodexTactics, Display, TEXT("HoloMarkerShots RESULT: %s -> %s"), Runner->Failures == 0 ? TEXT("PASS") : TEXT("FAIL"), *Runner->Dir);
		FCoreDelegates::OnEndFrame.Remove(Runner->EndFrameHandle);
		UDebugDrawService::Unregister(Runner->DrawHandle);
		FPlatformMisc::RequestExit(false, TEXT("HoloMarkerShots"));
	}

	void ShowPerfSet(bool bShow)
	{
		for (const TWeakObjectPtr<ATutorialMarkerActor>& Weak : Runner->PerfSet)
		{
			if (ATutorialMarkerActor* Marker = Weak.Get())
			{
				Marker->SetMarkerShown(bShow);
			}
		}
	}

	void Tick()
	{
		FRunner& R = *Runner;
		++R.Frame;
		++R.StepFrame;
		if (R.Frame > MaxFrames)
		{
			Check(false, FString::Printf(TEXT("timeout in step %d"), static_cast<int32>(R.Step)));
			Finish();
			return;
		}
		UWorld* World = FrontendSmokeUtils::FindGameWorld();
		UEnvironmentSubsystem* Environment = World ? UEnvironmentSubsystem::Get(World) : nullptr;
		USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr;
		if (!World || !Environment || !Environment->IsInitialised() || !Squad || !Squad->GetLeader())
		{
			return;
		}
		UWidgetLayoutLibrary::RemoveAllWidgets(World);
		const AWeatherPresenterActor* Presenter = AWeatherPresenterActor::Find(World);
		switch (R.Step)
		{
		case EStep::Warmup:
			if (R.StepFrame >= WarmupFrames && Presenter && Presenter->IsApplying() && ShadersPending() == 0)
			{
				const AOperativeCharacter* Leader = Squad->GetLeader();
				if (const APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
				{
					R.ViewYaw = PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraRotation().Yaw : R.ViewYaw;
				}
				const FRotator Yaw(0.f, R.ViewYaw, 0.f);
				R.Right = FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y);
				R.Centre = Ground(World, Leader->GetActorLocation() + FRotationMatrix(Yaw).GetUnitAxis(EAxis::X) * 650.f);
				R.OldMarker = SpawnMarker(World, R.Centre - R.Right * MarkerSpacing, true);
				R.NewMarker = SpawnMarker(World, R.Centre + R.Right * MarkerSpacing, true);
				if (ATutorialMarkerActor* Old = R.OldMarker.Get())
				{
					Old->ApplyLegacyLookForComparison();
				}
				for (int32 Index = 0; Index < PerfMarkers; ++Index)
				{
					const float Angle = 2.f * PI * Index / PerfMarkers;
					const FVector At = R.Centre + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * 520.f;
					R.PerfSet.Add(SpawnMarker(World, At, false));
				}
				CheckMaterials(World);
				GEngine->Exec(World, TEXT("DisableAllScreenMessages"));
				UE_LOG(LogCodexTactics, Display, TEXT("HoloMarkerShots: markers at (%.0f, %.0f, %.0f), view yaw %.0f, presenter %s"), R.Centre.X, R.Centre.Y, R.Centre.Z,
					R.ViewYaw, *Presenter->DescribeAdopted());
				R.Step = EStep::Setup;
				R.StepFrame = 0;
			}
			break;

		case EStep::Setup:
		{
			const FLook& Look = Looks[R.LookIndex];
			Environment->SetClockScale(0.f);
			Environment->ForceWeather(Look.Weather, true);
			Environment->SetHour(Look.Hour);
			R.Label = Look.Label;
			View(World);
			R.Step = EStep::Settle;
			R.StepFrame = 0;
			break;
		}

		case EStep::Settle:
			View(World);
			if (R.StepFrame >= SettleFrames && ShadersPending() == 0)
			{
				R.Step = EStep::Shot;
				R.StepFrame = 0;
			}
			break;

		case EStep::Shot:
		{
			View(World);
			const FLook& Look = Looks[R.LookIndex];
			FScreenshotRequest::RequestScreenshot(R.Dir / Look.File, /*bInShowUI*/ true, /*bAddFilenameSuffix*/ false);
			UE_LOG(LogCodexTactics, Display, TEXT("HoloMarkerShots: %s (%s)"), Look.File, *Environment->Describe());
			R.Step = EStep::Write;
			R.StepFrame = 0;
			break;
		}

		case EStep::Write:
			View(World);
			if (R.StepFrame >= 10)
			{
				CheckMaterials(World);
				++R.LookIndex;
				R.Step = R.LookIndex < UE_ARRAY_COUNT(Looks) ? EStep::Setup : EStep::PerfOff;
				R.StepFrame = 0;
				if (R.Step == EStep::PerfOff)
				{
					// The cost of the hologram at night: every comparison marker hidden vs 8 holograms in view.
					R.Label.Reset();
					R.OldMarker->SetMarkerShown(false);
					R.NewMarker->SetMarkerShown(false);
					ShowPerfSet(false);
					R.GpuSum = 0.0;
					R.Samples = 0;
				}
			}
			break;

		case EStep::PerfOff:
		case EStep::PerfOn:
			View(World);
			if (R.StepFrame > 20) // let the change settle
			{
				R.GpuSum += FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0));
				++R.Samples;
			}
			if (R.StepFrame >= 20 + MeasureFrames)
			{
				const double Ms = R.GpuSum / FMath::Max(R.Samples, 1);
				if (R.Step == EStep::PerfOff)
				{
					R.OffMs.Add(Ms);
					ShowPerfSet(true);
					R.Step = EStep::PerfOn;
				}
				else
				{
					R.OnMs.Add(Ms);
					ShowPerfSet(false);
					R.Step = ++R.PerfRound < PerfRounds ? EStep::PerfOff : EStep::Done;
					if (R.Step == EStep::Done)
					{
						R.OffMs.Sort();
						R.OnMs.Sort();
						const double Off = R.OffMs[R.OffMs.Num() / 2];
						const double On = R.OnMs[R.OnMs.Num() / 2];
						UE_LOG(LogCodexTactics, Display, TEXT("[HoloMarkerPerf] night, %d hologram markers in view: GPU median %.3f ms vs %.3f ms without (delta %+.3f ms; %d rounds x %d frames; off %s / on %s)"),
							PerfMarkers, On, Off, On - Off, PerfRounds, MeasureFrames,
							*FString::JoinBy(R.OffMs, TEXT(" "), [](double V) { return FString::Printf(TEXT("%.2f"), V); }),
							*FString::JoinBy(R.OnMs, TEXT(" "), [](double V) { return FString::Printf(TEXT("%.2f"), V); }));
					}
				}
				R.GpuSum = 0.0;
				R.Samples = 0;
				R.StepFrame = 0;
			}
			break;

		case EStep::Done:
			Finish();
			break;
		}
	}

	void Run(const TArray<FString>& /*Args*/, UWorld* /*World*/)
	{
		Runner = MakeShared<FRunner>();
		Runner->Dir = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir() / TEXT("..") / TEXT("HoloMarker"));
		FPaths::CollapseRelativeDirectories(Runner->Dir);
		IFileManager::Get().MakeDirectory(*Runner->Dir, true);
		Runner->DrawHandle = UDebugDrawService::Register(TEXT("Game"), FDebugDrawDelegate::CreateStatic(&OnDraw));
		Runner->EndFrameHandle = FCoreDelegates::OnEndFrame.AddStatic(&Tick);
		UE_LOG(LogCodexTactics, Display, TEXT("HoloMarkerShots: old vs hologram marker, day + night -> %s"), *Runner->Dir);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.HoloMarkerShots"),
		TEXT("Dev: rendered old vs hologram tutorial marker in Clear 13:00 and Snowfall 23:00 into Saved/Screenshots/HoloMarker, asserts every marker wears M_HoloMarker, logs the GPU cost of 8 markers; needs rendering, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
