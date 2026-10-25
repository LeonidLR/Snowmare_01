// Dev-only: mode "snowfx2" of CodexTactics.EnvironmentShots - rendered captures of the falling snow after the user feedback of 2026-10-23
// (docs/systems/environment-weather.md section 12.b: DA_SnowFX, smaller flakes, no snow indoors; UE-only, no Godot reference). Needs rendering:
//   UnrealEditor.exe CodexTactics.uproject /Game/Maps/L_Demo01_A_FrozenLake -LevelJson=L_Demo01_A_FrozenLake.json -game -RenderOffscreen
//     -ResX=1920 -ResY=1080 -nosound -benchmark -fps=30 -ExecCmds="CodexTactics.EnvironmentShots snowfx2 [noclip]"
// Output Saved/Screenshots/Environment/snowfx2/:
//   lake_<snowfall|blizzard>_<before|after>.png - the tactical view (pitch -35.26, vertical FOV 30, 26 m) on the squad leader, 12:00; "before"
//     = the first version's 8-18 cm flakes (a dev tuning override), "after" = DA_SnowFX; lake_blizzard_after_far.png at 40 m;
//   hut_zonecam_<killoff|killon>.png - the leader inside the fishermen's hut (Trigger_BoatsCabin), Blizzard, seen through the zone's own fixed
//     camera (what the player sees there), with Codex.Env.SnowIndoorKill 0 (the reported bug) and 1 (the fix); hut_tactical_killon.png - the
//     tactical view on the leader in the hut;
//   hut_tactical_killoff.png (the bug, the roof cut away over the leader);
//   clip/frame_NNNN.png (15 fps): 8 s of the lake blizzard + 6 s of the tactical view on the leader in the hut (encode: ffmpeg -framerate 15 -i frame_%04d.png ...).
// Weather forced instantly, clock stopped, the squad safe.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/CameraZoneVolume.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "CollisionQueryParams.h"
#include "Combat/HealthComponent.h"
#include "Debug/DebugDrawService.h"
#include "Debug/FrontendSmokeUtils.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Environment/EnvironmentRules.h"
#include "Environment/EnvironmentSubsystem.h"
#include "Environment/WeatherPresenterActor.h"
#include "Environment/WeatherSnowFxComponent.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

namespace EnvironmentSnowFx2Shots
{
	constexpr int32 MaxFrames = 30 * 900;
	constexpr int32 WarmupFrames = 150;
	constexpr int32 SettleFrames = 300;        // 10 s at -fps=30: the flakes of the previous look have all landed (life 6-9 s)
	constexpr int32 ClipEvery = 2;             // 15 fps
	constexpr float ViewPitch = -35.26f;
	constexpr float ViewVerticalFov = 30.f;
	constexpr float ViewDistance = 2600.f;
	constexpr float FarDistance = 4000.f;

	enum class EView : uint8 { Squad, HutZoneCamera, HutTactical };

	struct FShot
	{
		const TCHAR* Name;
		EWeatherState Weather;
		float Distance;
		bool bOldFlakes;
		EView View;
		bool bKill;
		int32 ClipFrames; // 0 = a still
	};

	const FShot Shots[] = {
		{ TEXT("lake_snowfall_before"), EWeatherState::Snowfall, ViewDistance, true, EView::Squad, true, 0 },
		{ TEXT("lake_blizzard_before"), EWeatherState::Blizzard, ViewDistance, true, EView::Squad, true, 0 },
		{ TEXT("lake_blizzard_after"), EWeatherState::Blizzard, ViewDistance, false, EView::Squad, true, 0 },
		{ TEXT("lake_blizzard_after_far"), EWeatherState::Blizzard, FarDistance, false, EView::Squad, true, 0 },
		{ TEXT("lake_snowfall_after"), EWeatherState::Snowfall, ViewDistance, false, EView::Squad, true, 0 },
		{ TEXT("clip_lake"), EWeatherState::Blizzard, ViewDistance, false, EView::Squad, true, 240 },
		{ TEXT("hut_tactical_killoff"), EWeatherState::Blizzard, ViewDistance, false, EView::HutTactical, false, 0 },
		{ TEXT("hut_zonecam_killoff"), EWeatherState::Blizzard, ViewDistance, false, EView::HutZoneCamera, false, 0 },
		{ TEXT("hut_zonecam_killon"), EWeatherState::Blizzard, ViewDistance, false, EView::HutZoneCamera, true, 0 },
		{ TEXT("hut_tactical_killon"), EWeatherState::Blizzard, ViewDistance, false, EView::HutTactical, true, 0 },
		{ TEXT("clip_hut"), EWeatherState::Blizzard, ViewDistance, false, EView::HutTactical, true, 180 },
	};

	enum class EStep : uint8 { Warmup, Setup, Settle, Clip, Write, Done };

	struct FRunner
	{
		EStep Step = EStep::Warmup;
		int32 Frame = 0;
		int32 StepFrame = 0;
		int32 ShotIndex = 0;
		int32 ClipCount = 0;
		bool bClips = true;
		float ViewYaw = 45.f;
		FString Dir;
		FString Label;
		FVector HutSpot = FVector::ZeroVector;
		TWeakObjectPtr<ACameraZoneVolume> Hut;
		TWeakObjectPtr<ACameraActor> Camera;
		FDelegateHandle EndFrameHandle;
		FDelegateHandle DrawHandle;
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

	void Capture(const FString& Path)
	{
		FScreenshotRequest::RequestScreenshot(Path, /*bInShowUI*/ true, /*bAddFilenameSuffix*/ false);
	}

	void OnDraw(UCanvas* Canvas, APlayerController*)
	{
		UFont* Font = GEngine ? GEngine->GetLargeFont() : nullptr;
		if (!Runner.IsValid() || !Canvas || !Font || Runner->Label.IsEmpty())
		{
			return;
		}
		const float Scale = Canvas->SizeX / 1920.f * 1.1f;
		float W = 0.f, H = 0.f;
		Canvas->TextSize(Font, Runner->Label, W, H, Scale, Scale);
		FCanvasTileItem Tile(FVector2D(12.f, 12.f), FVector2D(W + 24.f, H + 14.f), FLinearColor(0.f, 0.f, 0.f, 0.6f));
		Tile.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Tile);
		FCanvasTextItem Text(FVector2D(24.f, 19.f), FText::FromString(Runner->Label), Font, FLinearColor(1.f, 0.9f, 0.3f));
		Text.Scale = FVector2D(Scale, Scale);
		Text.EnableShadow(FLinearColor::Black);
		Canvas->DrawItem(Text);
	}

	void KeepSquadSafe(UWorld* World)
	{
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		for (AOperativeCharacter* Each : Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>())
		{
			Each->HealthComponent->SetMaxHealth(100000.f, false);
			Each->HealthComponent->Heal(100000.f);
			Each->bTacticalCeaseFire = true;
			Each->ColdLevel = 0.f;
		}
	}

	ACameraActor* OwnCamera(UWorld* World)
	{
		if (!Runner->Camera.IsValid())
		{
			Runner->Camera = World->SpawnActor<ACameraActor>(FVector::ZeroVector, FRotator::ZeroRotator);
		}
		return Runner->Camera.Get();
	}

	/** The tactical camera's numbers on the leader (own camera actor: a level / zone camera cannot take the view). */
	void ViewLeader(UWorld* World, float Distance)
	{
		APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		ACameraActor* Camera = OwnCamera(World);
		if (!PC || !Squad || !Squad->GetLeader() || !Camera)
		{
			return;
		}
		const FRotator Rotation(ViewPitch, Runner->ViewYaw, 0.f);
		const FVector Eye = Squad->GetLeader()->GetActorLocation() - Rotation.Vector() * Distance;
		const FVector2D Size = UWidgetLayoutLibrary::GetViewportSize(World);
		const float Aspect = Size.Y > 1.f ? Size.X / Size.Y : 16.f / 9.f;
		Camera->SetActorLocationAndRotation(Eye, Rotation);
		Camera->GetCameraComponent()->SetFieldOfView(FMath::RadiansToDegrees(2.f * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(ViewVerticalFov) * 0.5f) * Aspect)));
		Camera->GetCameraComponent()->bConstrainAspectRatio = false;
		if (PC->GetViewTarget() != Camera)
		{
			PC->SetViewTarget(Camera);
		}
	}

	/** The hut zone's own fixed camera (what the player sees inside); without one, a camera in the hut's corner looking across it. */
	void ViewHut(UWorld* World)
	{
		APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
		ACameraZoneVolume* Hut = Runner->Hut.Get();
		if (!PC || !Hut)
		{
			return;
		}
		AActor* Target = Hut->TargetCamera.Get();
		if (!Target)
		{
			ACameraActor* Camera = OwnCamera(World);
			const FTransform Zone = Hut->GetZoneTransform();
			const FVector Half = Hut->GetZoneHalfExtent();
			const FVector Eye = Zone.TransformPosition(FVector(Half.X * 0.8, Half.Y * 0.7, Half.Z * 0.6));
			Camera->SetActorLocationAndRotation(Eye, (Runner->HutSpot - Eye).Rotation());
			Camera->GetCameraComponent()->SetFieldOfView(80.f);
			Target = Camera;
		}
		if (PC->GetViewTarget() != Target)
		{
			PC->SetViewTarget(Target);
		}
	}

	ACameraZoneVolume* FindHut(UWorld* World, const FVector& Near)
	{
		ACameraZoneVolume* Best = nullptr;
		double BestDistance = TNumericLimits<double>::Max();
		for (TActorIterator<ACameraZoneVolume> It(World); It; ++It)
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

	void MoveLeaderIntoHut(UWorld* World)
	{
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		ACameraZoneVolume* Hut = Runner->Hut.Get();
		if (!Leader || !Hut)
		{
			return;
		}
		if (Runner->HutSpot.IsZero())
		{
			const FTransform Zone = Hut->GetZoneTransform();
			const FVector Half = Hut->GetZoneHalfExtent();
			// From the zone's middle height down (from the top a trace stops on the roof); the hut's floor blocks the visibility channel only.
			const FVector Spot = Zone.TransformPosition(FVector(-Half.X * 0.3, 0., 0.));
			FHitResult Hit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(SnowFx2HutFloor), true);
			for (AOperativeCharacter* Each : Squad->GetMembers())
			{
				Params.AddIgnoredActor(Each);
			}
			const bool bHit = World->LineTraceSingleByChannel(Hit, Spot, Spot - FVector(0., 0., Half.Z + 300.), ECC_Visibility, Params);
			Runner->HutSpot = (bHit ? Hit.ImpactPoint : Spot) + FVector(0., 0., 95.);
		}
		if (FVector::DistSquared(Leader->GetActorLocation(), Runner->HutSpot) > FMath::Square(60.))
		{
			Leader->SetActorLocation(Runner->HutSpot, false, nullptr, ETeleportType::TeleportPhysics);
		}
	}

	void ApplyShotView(UWorld* World, const FShot& Shot)
	{
		if (Shot.View == EView::Squad)
		{
			ViewLeader(World, Shot.Distance);
			return;
		}
		MoveLeaderIntoHut(World);
		if (Shot.View == EView::HutZoneCamera)
		{
			ViewHut(World);
		}
		else
		{
			ViewLeader(World, Shot.Distance);
		}
	}

	void SetLook(UWeatherSnowFxComponent* SnowFx, const FShot& Shot)
	{
		if (!SnowFx)
		{
			return;
		}
		if (Shot.bOldFlakes)
		{
			FWeatherSnowFxTuning Old = SnowFx->GetActiveTuning();
			Old.FlakeSizeMin = 8.f;
			Old.FlakeSizeMax = 18.f;
			SnowFx->SetTuningOverride(Old);
		}
		else
		{
			SnowFx->SetTuningOverride(TOptional<FWeatherSnowFxTuning>());
		}
	}

	void UpdateLabel(UEnvironmentSubsystem& Environment, const UWeatherSnowFxComponent* SnowFx, const FShot& Shot)
	{
		const FWeatherSnowFxTuning& Look = SnowFx ? SnowFx->GetActiveTuning() : FWeatherSnowFxTuning();
		const TCHAR* ViewName = Shot.View == EView::Squad ? TEXT("tactical") : Shot.View == EView::HutZoneCamera ? TEXT("hut zone camera") : TEXT("tactical, leader in the hut");
		Runner->Label = FString::Printf(TEXT("%s - %s 12:00 - %s %.0f m - flakes %.0f-%.0f cm%s - indoor kill %s - %d kill boxes"), Shot.Name,
			*EnvironmentRules::WeatherToString(Environment.GetWeather()), ViewName, Shot.Distance / 100.f, Look.FlakeSizeMin, Look.FlakeSizeMax,
			Shot.bOldFlakes ? TEXT(" (old)") : TEXT(" (DA_SnowFX)"), Shot.bKill ? TEXT("on") : TEXT("OFF"), SnowFx ? SnowFx->GetKillBoxes().Num() : 0);
	}

	void Finish()
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Env.SnowIndoorKill")))
		{
			Variable->Set(TEXT("1"), ECVF_SetByConsole);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("EnvironmentShots snowfx2: done (%d shots, %d clip frames) -> %s"), Runner->ShotIndex, Runner->ClipCount, *Runner->Dir);
		FCoreDelegates::OnEndFrame.Remove(Runner->EndFrameHandle);
		UDebugDrawService::Unregister(Runner->DrawHandle);
		FPlatformMisc::RequestExit(false, TEXT("EnvironmentShots"));
	}

	void Tick()
	{
		FRunner& S = *Runner;
		++S.Frame;
		++S.StepFrame;
		UWorld* World = FrontendSmokeUtils::FindGameWorld();
		if (S.Frame > MaxFrames)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("EnvironmentShots snowfx2: timeout in step %d"), static_cast<int32>(S.Step));
			Finish();
			return;
		}
		UEnvironmentSubsystem* Environment = World ? UEnvironmentSubsystem::Get(World) : nullptr;
		USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr;
		if (!World || !Environment || !Environment->IsInitialised() || !Squad || !Squad->GetLeader())
		{
			return;
		}
		KeepSquadSafe(World);
		UWidgetLayoutLibrary::RemoveAllWidgets(World);
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
		{
			if (AHUD* Hud = PC->GetHUD())
			{
				Hud->bShowHUD = false;
			}
		}
		AWeatherPresenterActor* Presenter = AWeatherPresenterActor::Find(World);
		UWeatherSnowFxComponent* SnowFx = Presenter ? Presenter->GetSnowFx() : nullptr;
		const int32 ShotCount = static_cast<int32>(UE_ARRAY_COUNT(Shots));
		// Skipped clips are passed over here.
		while (!S.bClips && S.ShotIndex < ShotCount && Shots[S.ShotIndex].ClipFrames > 0)
		{
			++S.ShotIndex;
		}
		const FShot& Shot = Shots[FMath::Min(S.ShotIndex, ShotCount - 1)];

		switch (S.Step)
		{
		case EStep::Warmup:
			if (S.StepFrame >= WarmupFrames && Presenter && Presenter->IsApplying() && ShadersPending() == 0)
			{
				if (const APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
				{
					S.ViewYaw = PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraRotation().Yaw : S.ViewYaw;
				}
				S.Hut = FindHut(World, Squad->GetLeader()->GetActorLocation());
				UE_LOG(LogCodexTactics, Display, TEXT("EnvironmentShots snowfx2: view yaw %.0f, hut %s, look from %s"), S.ViewYaw,
					S.Hut.IsValid() ? *S.Hut->GetActorNameOrLabel() : TEXT("none"), SnowFx && SnowFx->IsUsingSettingsAsset() ? TEXT("DA_SnowFX") : TEXT("C++ defaults"));
				S.Step = EStep::Setup;
				S.StepFrame = 0;
			}
			break;

		case EStep::Setup:
			if (S.ShotIndex >= ShotCount)
			{
				S.Step = EStep::Done;
				break;
			}
			Environment->SetClockScale(0.f);
			Environment->ForceWeather(Shot.Weather, true);
			Environment->SetHour(12.f);
			if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Env.SnowIndoorKill")))
			{
				Variable->Set(Shot.bKill ? TEXT("1") : TEXT("0"), ECVF_SetByConsole);
			}
			SetLook(SnowFx, Shot);
			ApplyShotView(World, Shot);
			S.Step = EStep::Settle;
			S.StepFrame = 0;
			break;

		case EStep::Settle:
			ApplyShotView(World, Shot);
			UpdateLabel(*Environment, SnowFx, Shot);
			if (!(SnowFx && SnowFx->IsSnowActive()) && S.StepFrame < SettleFrames * 4)
			{
				S.StepFrame = FMath::Min(S.StepFrame, 1);
			}
			if (S.StepFrame >= SettleFrames && ShadersPending() == 0)
			{
				if (Shot.ClipFrames > 0)
				{
					S.Step = EStep::Clip;
				}
				else
				{
					Capture(S.Dir / (FString(Shot.Name) + TEXT(".png")));
					UE_LOG(LogCodexTactics, Display, TEXT("EnvironmentShots snowfx2: %s (%s; %s)"), Shot.Name, *Environment->Describe(), *S.Label);
					S.Step = EStep::Write;
				}
				S.StepFrame = 0;
			}
			break;

		case EStep::Clip:
			ApplyShotView(World, Shot);
			UpdateLabel(*Environment, SnowFx, Shot);
			if (S.StepFrame % ClipEvery == 0 && S.StepFrame <= Shot.ClipFrames)
			{
				Capture(S.Dir / TEXT("clip") / FString::Printf(TEXT("frame_%04d.png"), S.ClipCount++));
			}
			if (S.StepFrame > Shot.ClipFrames + 3)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("EnvironmentShots snowfx2: %s, %d frames so far"), Shot.Name, S.ClipCount);
				S.Step = EStep::Write;
				S.StepFrame = 0;
			}
			break;

		case EStep::Write:
			ApplyShotView(World, Shot);
			if (S.StepFrame >= 10)
			{
				++S.ShotIndex;
				S.Step = EStep::Setup;
				S.StepFrame = 0;
			}
			break;

		case EStep::Done:
			if (S.StepFrame >= 10)
			{
				Finish();
			}
			break;
		}
	}

	void Run(const TArray<FString>& Args)
	{
		Runner = MakeShared<FRunner>();
		Runner->bClips = !Args.ContainsByPredicate([](const FString& Arg) { return Arg.Equals(TEXT("noclip"), ESearchCase::IgnoreCase); });
		Runner->Dir = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir() / TEXT("..") / TEXT("Environment") / TEXT("snowfx2"));
		FPaths::CollapseRelativeDirectories(Runner->Dir);
		IFileManager::Get().DeleteDirectory(*(Runner->Dir / TEXT("clip")), false, true);
		IFileManager::Get().MakeDirectory(*Runner->Dir, true);
		Runner->DrawHandle = UDebugDrawService::Register(TEXT("Game"), FDebugDrawDelegate::CreateStatic(&OnDraw));
		Runner->EndFrameHandle = FCoreDelegates::OnEndFrame.AddStatic(&Tick);
		UE_LOG(LogCodexTactics, Display, TEXT("EnvironmentShots snowfx2: %d shots -> %s"), static_cast<int32>(UE_ARRAY_COUNT(Shots)), *Runner->Dir);
	}
}

#endif // !UE_BUILD_SHIPPING
