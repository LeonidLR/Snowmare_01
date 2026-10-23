// Dev-only: mode "terrain" of CodexTactics.EnvironmentShots (dispatched from EnvironmentShotsCommand.cpp) - rendered proof of the fixed
// landscape material M_Landscape_Master / MI_Landscape_Master and its snow functions (environment §13, docs/systems/environment-weather.md).
// Run on the agents' landscape test map (Scripts/Editor/create_snow_terrain_test_map.py), offscreen, fixed 30 fps:
//   UnrealEditor.exe CodexTactics.uproject /Game/Environment/Weather/Test/L_SnowTerrainTest -game -RenderOffscreen -ResX=1280 -ResY=720
//     -nosound -benchmark -fps=30 -ExecCmds="CodexTactics.EnvironmentShots terrain [nomovie] [notracks]"
// Output (Saved/Screenshots/Environment/terrain/):
//   terrain_snow_000 / _050 / _100.png   Clear 12:00, an overview (flat = snow, the steep north ridge = rock, the east bowl below -250 cm =
//                                         ice) with SnowAmount 0 / 0.5 / 1 (MF_SnowPowder whitens rock and ice as the cover grows)
//   night_frost.png / night_fresh.png     23:00, SnowAmount 0.6, a closer view: Clear (FrostAmount from the preset, FreshSnow 0) vs Snowfall
//                                         with FreshSnow driven to 1 by the interactive-snow subsystem (time dilation while it rises)
//   tracks_loop.png / tracks_close.png /  the leader walked an 8-point loop (operative footprints from USnowInteractionSubsystem) and a
//   crater_close.png / tracks_off.png     forced Crater stamp next to it (AddStamp); tracks_off = the same view with Codex.Env.SnowTracks 0
//                                         (the window disabled = no effect). Skipped (logged) when the subsystem is not active.
//   movie/frame_NNNN.png                  10 s at 15 fps: the camera orbits 40 degrees while SnowAmount rises 0 -> 1 and the leader walks the
//                                         loop (encode: ffmpeg -y -framerate 15 -i frame_%04d.png -vf scale=960:-2 -c:v libx264 -pix_fmt yuv420p
//                                         -crf 23 terrain_10s.mp4)
// A label on the debug canvas shows the weather, hour and the MPC_Environment values the material reads. No Godot reference.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "CanvasItem.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Debug/DebugDrawService.h"
#include "Debug/FrontendSmokeUtils.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "Environment/EnvironmentRules.h"
#include "Environment/EnvironmentSubsystem.h"
#include "Environment/SnowInteractionRules.h"
#include "Environment/SnowInteractionSubsystem.h"
#include "Environment/WeatherPresenterActor.h"
#include "GameFramework/HUD.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

namespace EnvironmentTerrainShots
{
	constexpr int32 MaxFrames = 30 * 400;
	constexpr int32 WarmupFrames = 150;
	constexpr int32 QuietFrames = 45;          // no shader job for this long = the landscape material is compiled
	constexpr int32 SettleFrames = 75;
	constexpr int32 MovieFrames = 300;         // 10 s at -fps=30
	constexpr int32 MovieEvery = 2;            // 15 fps
	constexpr int32 LoopPoints = 8;
	constexpr float LoopRadiusCm = 450.f;
	constexpr int32 LoopPointFrames = 150;
	constexpr float FreshDilation = 30.f;      // FreshSnow 0 -> 1 takes 120 s of snowfall: 4 s real
	constexpr int32 FreshRiseFrames = 165;
	const TCHAR* MpcPath = TEXT("/Game/Environment/Weather/MPC/MPC_Environment.MPC_Environment");
	/** The overview: the plateau with the ridge ahead (+X), the bowl to the right (+Y) and the slope to the left (-Y). */
	const FVector OverviewFocus(300.f, 300.f, 0.f);

	enum class EStep : uint8
	{
		Warmup,
		Overview,
		NightFrost,
		FreshRise,
		NightFresh,
		TrackWalk,
		TrackShots,
		MovieSetup,
		Movie,
		Done
	};

	struct FShotView
	{
		FVector Focus = FVector::ZeroVector;
		float Yaw = 0.f;
		float Pitch = -35.26f;
		float Distance = 2600.f;
		float HorizontalFov = 60.f;
	};

	struct FTerrainRunner
	{
		EStep Step = EStep::Warmup;
		int32 Frame = 0;
		int32 StepFrame = 0;
		int32 Index = 0;
		int32 Quiet = 0;
		int32 ClipCount = 0;
		int32 LoopIndex = 0;
		bool bMovie = true;
		bool bTracks = true;
		bool bTracksActive = false;
		FString Dir;
		FVector Home = FVector::ZeroVector;
		FShotView View;
		TArray<FString> Lines;
		TArray<FString> Report;
		TWeakObjectPtr<ACameraActor> Camera;
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<UMaterialParameterCollection> Mpc;
		FDelegateHandle EndFrameHandle;
		FDelegateHandle DrawHandle;
	};

	TSharedPtr<FTerrainRunner> TRunner;

	int32 ShadersPending()
	{
#if WITH_EDITOR
		return GShaderCompilingManager ? GShaderCompilingManager->GetNumRemainingJobs() : 0;
#else
		return 0;
#endif
	}

	float MpcScalar(UWorld* World, const TCHAR* Name)
	{
		UMaterialParameterCollection* Mpc = TRunner->Mpc.Get();
		return Mpc && Mpc->GetScalarParameterByName(Name) ? UKismetMaterialLibrary::GetScalarParameterValue(World, Mpc, Name) : -1.f;
	}

	FLinearColor MpcVector(UWorld* World, const TCHAR* Name)
	{
		UMaterialParameterCollection* Mpc = TRunner->Mpc.Get();
		return Mpc && Mpc->GetVectorParameterByName(Name) ? UKismetMaterialLibrary::GetVectorParameterValue(World, Mpc, Name) : FLinearColor(0.f, 0.f, 0.f, -1.f);
	}

	void SetCVar(const TCHAR* Name, int32 Value)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Variable->Set(Value, ECVF_SetByConsole);
		}
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
		if (UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>())
		{
			if (Waves->GetAliveEnemyCount() > 0)
			{
				Waves->ClearAllEnemies();
			}
		}
	}

	/** An own camera actor (a level / tutorial camera cannot take the view), re-aimed every frame. */
	void ApplyView(UWorld* World)
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
		const FShotView& View = TRunner->View;
		const FRotator Rotation(View.Pitch, View.Yaw, 0.f);
		const FVector Eye = View.Focus - Rotation.Vector() * View.Distance;
		if (!TRunner->Camera.IsValid())
		{
			TRunner->Camera = World->SpawnActor<ACameraActor>(Eye, Rotation);
		}
		if (ACameraActor* Camera = TRunner->Camera.Get())
		{
			Camera->SetActorLocationAndRotation(Eye, Rotation);
			Camera->GetCameraComponent()->SetFieldOfView(View.HorizontalFov);
			Camera->GetCameraComponent()->bConstrainAspectRatio = false;
			if (PC->GetViewTarget() != Camera)
			{
				PC->SetViewTarget(Camera);
			}
		}
	}

	/** The tactical camera's numbers (pitch -35.26, vertical FOV 30) on Focus from Distance. */
	FShotView TacticalView(UWorld* World, const FVector& Focus, float Yaw, float Distance)
	{
		const FVector2D Size = UWidgetLayoutLibrary::GetViewportSize(World);
		const float Aspect = Size.Y > 1.f ? Size.X / Size.Y : 16.f / 9.f;
		FShotView View;
		View.Focus = Focus;
		View.Yaw = Yaw;
		View.Distance = Distance;
		View.HorizontalFov = FMath::RadiansToDegrees(2.f * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(30.f) * 0.5f) * Aspect));
		return View;
	}

	FShotView OverviewView()
	{
		FShotView View;
		View.Focus = OverviewFocus;
		View.Yaw = 0.f;
		View.Pitch = -30.f;
		View.Distance = 3800.f;
		View.HorizontalFov = 70.f;
		return View;
	}

	FString HourText(float Hour)
	{
		const int32 Total = FMath::FloorToInt(FMath::Fmod(Hour, 24.f) * 60.f);
		return FString::Printf(TEXT("%02d:%02d"), Total / 60, Total % 60);
	}

	void Label(UWorld* World, UEnvironmentSubsystem& Environment, const FString& What)
	{
		const FLinearColor Window = MpcVector(World, TEXT("SnowTrackWindow"));
		TRunner->Lines.Reset();
		TRunner->Lines.Add(FString::Printf(TEXT("L_SnowTerrainTest - %s %s - %s"), *EnvironmentRules::WeatherToString(Environment.GetWeather()),
			*HourText(Environment.GetHour()), *What));
		TRunner->Lines.Add(FString::Printf(TEXT("MPC SnowAmount %.2f  FreshSnow %.2f  FrostAmount %.2f  track window %s"), MpcScalar(World, TEXT("SnowAmount")),
			MpcScalar(World, TEXT("FreshSnow")), MpcScalar(World, TEXT("FrostAmount")), Window.A > 0.5f ? TEXT("on") : (Window.A < 0.f ? TEXT("n/a") : TEXT("off"))));
	}

	void OnDraw(UCanvas* Canvas, APlayerController* /*Controller*/)
	{
		if (!TRunner.IsValid() || !Canvas || !GEngine || TRunner->Lines.IsEmpty())
		{
			return;
		}
		UFont* Font = GEngine->GetMediumFont();
		const float Scale = FMath::Clamp(Canvas->SizeX / 1280.f, 0.6f, 1.6f);
		float Y = 12.f;
		FCanvasTileItem Tile(FVector2D(10.f, 8.f), FVector2D(Canvas->SizeX - 20.f, 22.f * Scale * TRunner->Lines.Num() + 10.f), FLinearColor(0.f, 0.f, 0.f, 0.55f));
		Tile.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Tile);
		for (const FString& Line : TRunner->Lines)
		{
			FCanvasTextItem Item(FVector2D(18.f, Y), FText::FromString(Line), Font, FLinearColor(1.f, 0.9f, 0.35f));
			Item.Scale = FVector2D(Scale, Scale);
			Item.EnableShadow(FLinearColor::Black);
			Canvas->DrawItem(Item);
			Y += 22.f * Scale;
		}
	}

	void Capture(const FString& File)
	{
		FScreenshotRequest::RequestScreenshot(TRunner->Dir / File, /*bInShowUI*/ true, /*bAddFilenameSuffix*/ false);
	}

	void Finish()
	{
		for (const FString& Line : TRunner->Report)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[TerrainShots] %s"), *Line);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("EnvironmentShots terrain: done (%d movie frames) -> %s"), TRunner->ClipCount, *TRunner->Dir);
		SetCVar(TEXT("Codex.Env.SnowTracks"), 1);
		FCoreDelegates::OnEndFrame.Remove(TRunner->EndFrameHandle);
		UDebugDrawService::Unregister(TRunner->DrawHandle);
		FPlatformMisc::RequestExit(false, TEXT("EnvironmentShots terrain"));
	}

	void SetConditions(UEnvironmentSubsystem& Environment, EWeatherState Weather, float Hour, float SnowAmount)
	{
		Environment.SetClockScale(0.f);
		if (Environment.GetWeather() != Weather || Environment.GetTransitionBlend() > 0.f)
		{
			Environment.ForceWeather(Weather, true);
		}
		if (!FMath::IsNearlyEqual(Environment.GetHour(), Hour, 0.01f))
		{
			Environment.SetHour(Hour);
		}
		if (!FMath::IsNearlyEqual(Environment.GetSnowAmount(), SnowAmount, 0.0005f))
		{
			Environment.SetSnowAmount(SnowAmount);
		}
	}

	/** The leader's next waypoint on the loop around Home; true once the loop is closed. */
	bool WalkLoop(AOperativeCharacter& Leader, int32& LoopIndex, int32 StepFrame, int32& PointFrames)
	{
		const FVector Target = TRunner->Home + FVector(LoopRadiusCm, 0.f, 0.f).RotateAngleAxis(360.f * LoopIndex / LoopPoints, FVector::UpVector);
		++PointFrames;
		if (PointFrames == 1)
		{
			Leader.OrderMoveTo(Target, false);
		}
		if (FVector::Dist2D(Leader.GetActorLocation(), Target) < 80.f || PointFrames > LoopPointFrames)
		{
			++LoopIndex;
			PointFrames = 0;
		}
		return LoopIndex > LoopPoints;
	}

	int32 GPointFrames = 0;

	void Tick()
	{
		FTerrainRunner& R = *TRunner;
		++R.Frame;
		++R.StepFrame;
		UWorld* World = FrontendSmokeUtils::FindGameWorld();
		if (R.Frame > MaxFrames)
		{
			R.Report.Add(FString::Printf(TEXT("timeout in step %d"), static_cast<int32>(R.Step)));
			Finish();
			return;
		}
		UEnvironmentSubsystem* Environment = World ? UEnvironmentSubsystem::Get(World) : nullptr;
		USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr;
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!World || !Environment || !Environment->IsInitialised() || !Leader)
		{
			return;
		}
		R.World = World;
		KeepSquadSafe(World);
		UWidgetLayoutLibrary::RemoveAllWidgets(World);
		const AWeatherPresenterActor* Presenter = AWeatherPresenterActor::Find(World);
		USnowInteractionSubsystem* Tracks = USnowInteractionSubsystem::Get(World);
		R.Quiet = ShadersPending() == 0 ? R.Quiet + 1 : 0;

		switch (R.Step)
		{
		case EStep::Warmup:
			SetConditions(*Environment, EWeatherState::Clear, 12.f, 0.f);
			R.View = OverviewView();
			ApplyView(World);
			Label(World, *Environment, TEXT("compiling shaders"));
			if (R.StepFrame >= WarmupFrames && Presenter && Presenter->IsApplying() && R.Quiet >= QuietFrames)
			{
				R.Home = Leader->GetActorLocation();
				R.bTracksActive = R.bTracks && Tracks && Tracks->IsActive();
				R.Report.Add(FString::Printf(TEXT("ready after %d frames; leader at %s; interactive snow %s; presenter %s"), R.Frame, *R.Home.ToCompactString(),
					R.bTracksActive ? TEXT("active") : (Tracks ? TEXT("present, not active") : TEXT("missing")), *Presenter->DescribeAdopted()));
				R.Step = EStep::Overview;
				R.StepFrame = 0;
				R.Index = 0;
			}
			break;

		case EStep::Overview:
		{
			static const float Amounts[] = { 0.f, 0.5f, 1.f };
			const float Amount = Amounts[R.Index];
			SetConditions(*Environment, EWeatherState::Clear, 12.f, Amount);
			R.View = OverviewView();
			ApplyView(World);
			Label(World, *Environment, FString::Printf(TEXT("overview: flat = snow, north ridge = rock, east bowl = ice - SnowAmount %.1f"), Amount));
			if (R.StepFrame == SettleFrames)
			{
				const FString File = FString::Printf(TEXT("terrain_snow_%03d.png"), FMath::RoundToInt(Amount * 100.f));
				Capture(File);
				R.Report.Add(FString::Printf(TEXT("%s (%s)"), *File, *Environment->Describe()));
			}
			if (R.StepFrame >= SettleFrames + 10)
			{
				++R.Index;
				R.StepFrame = 0;
				if (R.Index >= UE_ARRAY_COUNT(Amounts))
				{
					R.Step = EStep::NightFrost;
				}
			}
			break;
		}

		case EStep::NightFrost:
			SetConditions(*Environment, EWeatherState::Clear, 23.f, 0.6f);
			R.View = TacticalView(World, R.Home + FVector(300.f, 300.f, 0.f), 30.f, 1500.f);
			ApplyView(World);
			Label(World, *Environment, TEXT("night, Clear: frost (low roughness + glints), no fresh snow"));
			if (R.StepFrame == SettleFrames * 2)
			{
				Capture(TEXT("night_frost.png"));
				R.Report.Add(FString::Printf(TEXT("night_frost.png (%s, FreshSnow %.2f, FrostAmount %.2f)"), *Environment->Describe(), MpcScalar(World, TEXT("FreshSnow")),
					MpcScalar(World, TEXT("FrostAmount"))));
			}
			if (R.StepFrame >= SettleFrames * 2 + 10)
			{
				R.Step = EStep::FreshRise;
				R.StepFrame = 0;
			}
			break;

		case EStep::FreshRise:
			// The interactive-snow subsystem raises FreshSnow while it snows (120 s to 1): run the world faster for a few seconds.
			SetConditions(*Environment, EWeatherState::Snowfall, 23.f, 0.6f);
			World->GetWorldSettings()->SetTimeDilation(FreshDilation);
			ApplyView(World);
			Label(World, *Environment, TEXT("night, Snowfall: fresh snow rising"));
			if (R.StepFrame >= FreshRiseFrames || MpcScalar(World, TEXT("FreshSnow")) >= 0.999f)
			{
				World->GetWorldSettings()->SetTimeDilation(1.f);
				R.Step = EStep::NightFresh;
				R.StepFrame = 0;
			}
			break;

		case EStep::NightFresh:
			SetConditions(*Environment, EWeatherState::Snowfall, 23.f, 0.6f);
			ApplyView(World);
			Label(World, *Environment, TEXT("night, Snowfall: fresh snow (brighter, rougher, micro normal)"));
			if (R.StepFrame == SettleFrames * 2)
			{
				Capture(TEXT("night_fresh.png"));
				R.Report.Add(FString::Printf(TEXT("night_fresh.png (%s, FreshSnow %.2f, FrostAmount %.2f)"), *Environment->Describe(), MpcScalar(World, TEXT("FreshSnow")),
					MpcScalar(World, TEXT("FrostAmount"))));
			}
			if (R.StepFrame >= SettleFrames * 2 + 10)
			{
				R.Step = R.bTracksActive ? EStep::TrackWalk : (R.bMovie ? EStep::MovieSetup : EStep::Done);
				if (!R.bTracksActive)
				{
					R.Report.Add(TEXT("tracks: skipped (USnowInteractionSubsystem not active or 'notracks')"));
				}
				R.StepFrame = 0;
				R.LoopIndex = 0;
				GPointFrames = 0;
			}
			break;

		case EStep::TrackWalk:
			SetConditions(*Environment, EWeatherState::Clear, 15.5f, 1.f);
			R.View = TacticalView(World, R.Home, 30.f, 2600.f);
			ApplyView(World);
			Label(World, *Environment, FString::Printf(TEXT("interactive snow: the leader walks a loop (%d / %d)"), FMath::Min(R.LoopIndex, LoopPoints), LoopPoints));
			if (R.StepFrame == 1 && Tracks)
			{
				Tracks->ClearTracks();
			}
			if (WalkLoop(*Leader, R.LoopIndex, R.StepFrame, GPointFrames))
			{
				if (Tracks)
				{
					// A forced crater 4 m beside the loop (the explosion stamp of phase 3, world-aligned).
					const FVector Crater = R.Home + FVector(-150.f, -750.f, 0.f);
					Tracks->AddStamp(SnowInteractionRules::MakePrint(Tracks->GetStamps().Crater, FVector2D(Crater.X, Crater.Y), 0.f));
				}
				R.Step = EStep::TrackShots;
				R.StepFrame = 0;
				R.Index = 0;
			}
			break;

		case EStep::TrackShots:
		{
			struct FTrackShot { const TCHAR* File; FVector Offset; float Distance; float Pitch; bool bTracks; };
			static const FTrackShot Shots[] = {
				{ TEXT("tracks_loop.png"), FVector(0.f, -250.f, 0.f), 2600.f, -35.26f, true },
				{ TEXT("tracks_close.png"), FVector(0.f, 0.f, 0.f), 1100.f, -50.f, true },
				{ TEXT("crater_close.png"), FVector(-150.f, -750.f, 0.f), 1300.f, -45.f, true },
				{ TEXT("tracks_off.png"), FVector(0.f, -250.f, 0.f), 2600.f, -35.26f, false },
			};
			const FTrackShot& Shot = Shots[R.Index];
			SetConditions(*Environment, EWeatherState::Clear, 15.5f, 1.f);
			if (R.StepFrame == 1)
			{
				SetCVar(TEXT("Codex.Env.SnowTracks"), Shot.bTracks ? 1 : 0);
			}
			R.View = TacticalView(World, R.Home + Shot.Offset, 30.f, Shot.Distance);
			R.View.Pitch = Shot.Pitch;
			ApplyView(World);
			Label(World, *Environment, FString::Printf(TEXT("interactive snow %s: footprints of the loop + a forced crater"), Shot.bTracks ? TEXT("ON") : TEXT("OFF (Codex.Env.SnowTracks 0)")));
			if (R.StepFrame == SettleFrames)
			{
				Capture(Shot.File);
				const FLinearColor Window = MpcVector(World, TEXT("SnowTrackWindow"));
				R.Report.Add(FString::Printf(TEXT("%s (window origin %.0f, %.0f size %.0f m enabled %.0f; stamps drawn %lld)"), Shot.File, Window.R, Window.G,
					Window.B > 0.f ? 0.01f / Window.B : 0.f, Window.A, Tracks ? Tracks->GetMetrics().StampsDrawn : 0ll));
			}
			if (R.StepFrame >= SettleFrames + 10)
			{
				++R.Index;
				R.StepFrame = 0;
				if (R.Index >= UE_ARRAY_COUNT(Shots))
				{
					SetCVar(TEXT("Codex.Env.SnowTracks"), 1);
					R.Step = R.bMovie ? EStep::MovieSetup : EStep::Done;
				}
			}
			break;
		}

		case EStep::MovieSetup:
			SetConditions(*Environment, EWeatherState::Clear, 15.5f, 0.f);
			R.View = TacticalView(World, R.Home, 10.f, 2600.f);
			ApplyView(World);
			Label(World, *Environment, TEXT("10 s: SnowAmount 0 -> 1, the leader walks the loop"));
			if (R.StepFrame == 1 && Tracks)
			{
				Tracks->ClearTracks();
			}
			if (R.StepFrame >= 30)
			{
				R.Step = EStep::Movie;
				R.StepFrame = 0;
				R.LoopIndex = 0;
				GPointFrames = 0;
				R.ClipCount = 0;
			}
			break;

		case EStep::Movie:
		{
			const float T = FMath::Clamp(static_cast<float>(R.StepFrame) / MovieFrames, 0.f, 1.f);
			SetConditions(*Environment, EWeatherState::Clear, 15.5f, T);
			R.View = TacticalView(World, R.Home, 10.f + 40.f * T, 2600.f);
			ApplyView(World);
			if (R.LoopIndex <= LoopPoints)
			{
				WalkLoop(*Leader, R.LoopIndex, R.StepFrame, GPointFrames);
			}
			Label(World, *Environment, FString::Printf(TEXT("10 s: SnowAmount %.2f, the leader walks the loop"), T));
			if (R.StepFrame % MovieEvery == 0 && R.StepFrame <= MovieFrames)
			{
				Capture(FString::Printf(TEXT("movie/frame_%04d.png"), R.ClipCount++));
			}
			if (R.StepFrame > MovieFrames + 5)
			{
				R.Step = EStep::Done;
				R.StepFrame = 0;
			}
			break;
		}

		case EStep::Done:
			if (R.StepFrame >= 10)
			{
				Finish();
			}
			break;
		}
	}

	void Run(const TArray<FString>& Args)
	{
		auto Has = [&Args](const TCHAR* Word)
		{
			return Args.ContainsByPredicate([Word](const FString& Arg) { return Arg.Equals(Word, ESearchCase::IgnoreCase); });
		};
		TRunner = MakeShared<FTerrainRunner>();
		TRunner->bMovie = !Has(TEXT("nomovie"));
		TRunner->bTracks = !Has(TEXT("notracks"));
		TRunner->Dir = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir() / TEXT("..") / TEXT("Environment") / TEXT("terrain"));
		FPaths::CollapseRelativeDirectories(TRunner->Dir);
		IFileManager::Get().DeleteDirectory(*(TRunner->Dir / TEXT("movie")), false, true);
		IFileManager::Get().MakeDirectory(*TRunner->Dir, true);
		TRunner->Mpc = LoadObject<UMaterialParameterCollection>(nullptr, MpcPath);
		TRunner->DrawHandle = UDebugDrawService::Register(TEXT("Game"), FDebugDrawDelegate::CreateStatic(&OnDraw));
		TRunner->EndFrameHandle = FCoreDelegates::OnEndFrame.AddStatic(&Tick);
		UE_LOG(LogCodexTactics, Display, TEXT("EnvironmentShots terrain: stills%s%s -> %s"), TRunner->bTracks ? TEXT(" + tracks") : TEXT(""),
			TRunner->bMovie ? TEXT(" + movie") : TEXT(""), *TRunner->Dir);
	}
}

#endif // !UE_BUILD_SHIPPING
