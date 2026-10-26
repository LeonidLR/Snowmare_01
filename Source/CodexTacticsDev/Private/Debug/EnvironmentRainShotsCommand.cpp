// Dev-only: modes "rain" and "rainperf" of CodexTactics.EnvironmentShots - rendered captures of the RAIN visuals of a temperate level
// (docs/systems/environment-weather.md section 18; UE-only). Needs rendering; run on the temperate test map:
//   UnrealEditor.exe CodexTactics.uproject /Game/Environment/Weather/Test/L_WeatherRainTest -game -RenderOffscreen -ResX=1920 -ResY=1080
//     -nosound -benchmark -fps=30 -ExecCmds="CodexTactics.EnvironmentShots rain [noclip]"
//   ... -fps=60 -ExecCmds="CodexTactics.EnvironmentShots rainperf"
// Mode "rain" -> Saved/Screenshots/Environment/rain/:
//   01_sunny_1200.png, 02_rain_start_dry.png, 03_rain_wet050.png, 04_rain_wet100_puddles050.png, 05_rain_wet100_puddles100.png,
//   06_storm_lightning.png (a strike forced 1 frame before), 07_storm_noflash.png, 08_rain_screendrops_default.png,
//   09_rain_screendrops_x3.png (DA strength x3, to show the effect), 10_rain_close_15m.png, 11_storm_far_40m.png, 12_rain_night_2300.png,
//   13_hut_storm.png (the leader in the hut: no rain under the roof); clip/frame_NNNN.png (10 s at 15 fps: SunnyDay -> rain starts, the
//   ground gets wet, puddles form, storm + a strike; encode: ffmpeg -framerate 15 -i frame_%04d.png -c:v libx264 -pix_fmt yuv420p rain_10s.mp4).
//   The tactical camera's numbers (pitch -35.26, vertical FOV 30, 26 m) on the squad leader; clock stopped; lightning only where forced.
// Mode "rainperf" -> log lines "[RainPerf]" (GPU / render / game ms, draws, prims; averaged over 180 frames after a 6 s settle) and stills
//   Saved/Screenshots/Environment/rainperf/<case>[_far].png for sunny (dry), rain (wet 1, puddles 0.5), storm (wet 1, puddles 1),
//   storm_fxoff (the rain / puddle systems hidden, screen drops off: the particles' cost), storm_nooverlay (Codex.Env.SnowOverlay /
//   SnowGround 0: the wet overlays' cost) at 26 m and 40 m.
// Mode "rainterrain" (on /Game/Environment/Weather/Test/L_SnowTerrainTest, switched to temperate by Codex.Env.Climate) -> rain/terrain/:
//   t1_terrain_dry .. t5_terrain_rain_close_15m - the landscape's MF_RainWetSurface (wet darkening, puddles, ripples).
// Mode "rainprobe" (look tuning) -> rainprobe/: the storm with drop look variants (wide / bright / swapped axes) and screen-drop tilings.
// Captures run with "r.SetRes 1920x1080w,DisableAllScreenMessages" first in -ExecCmds (the project config sets 1280x720).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Debug/DebugDrawService.h"
#include "Debug/FrontendSmokeUtils.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "Environment/EnvironmentRules.h"
#include "Environment/EnvironmentSubsystem.h"
#include "Environment/WeatherPresenterActor.h"
#include "Environment/WeatherRainFxComponent.h"
#include "Environment/WeatherSnowOverlayComponent.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "RHI.h"
#include "RHIStats.h"
#include "UnrealClient.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

namespace EnvironmentRainShots
{
	constexpr int32 MaxFrames = 60 * 900;
	constexpr int32 WarmupFrames = 120;
	constexpr float ViewPitch = -35.26f;
	constexpr float ViewVerticalFov = 30.f;
	constexpr float ViewDistance = 2600.f;
	constexpr float CloseDistance = 1500.f;
	constexpr float FarDistance = 4000.f;
	/** Where the leader stands for the stills: south-west of the hut, the hut, the crates and the open floor in view. */
	const FVector LeaderSpot(350., -250., 95.);
	const FVector HutSpot(900., 300., 95.);

	enum class EKind : uint8 { Still, Strike, DropsBoost, Hut, Clip, Probe };

	struct FShot
	{
		const TCHAR* Name;
		EWeatherState Weather;
		float Hour;
		float Wetness;
		float Puddles;
		float Distance;
		EKind Kind;
	};

	const FShot Shots[] = {
		{ TEXT("01_sunny_1200"), EWeatherState::Clear, 12.f, 0.f, 0.f, ViewDistance, EKind::Still },
		{ TEXT("02_rain_start_dry"), EWeatherState::Snowfall, 12.f, 0.f, 0.f, ViewDistance, EKind::Still },
		{ TEXT("03_rain_wet050"), EWeatherState::Snowfall, 12.f, 0.5f, 0.f, ViewDistance, EKind::Still },
		{ TEXT("04_rain_wet100_puddles050"), EWeatherState::Snowfall, 12.f, 1.f, 0.5f, ViewDistance, EKind::Still },
		{ TEXT("05_rain_wet100_puddles100"), EWeatherState::Snowfall, 12.f, 1.f, 1.f, ViewDistance, EKind::Still },
		{ TEXT("06_storm_lightning"), EWeatherState::Blizzard, 12.f, 1.f, 1.f, ViewDistance, EKind::Strike },
		{ TEXT("07_storm_noflash"), EWeatherState::Blizzard, 12.f, 1.f, 1.f, ViewDistance, EKind::Still },
		{ TEXT("08_rain_screendrops_default"), EWeatherState::Snowfall, 12.f, 1.f, 0.5f, ViewDistance, EKind::Still },
		{ TEXT("09_rain_screendrops_x3"), EWeatherState::Snowfall, 12.f, 1.f, 0.5f, ViewDistance, EKind::DropsBoost },
		{ TEXT("10_rain_close_15m"), EWeatherState::Snowfall, 12.f, 1.f, 1.f, CloseDistance, EKind::Still },
		{ TEXT("11_storm_far_40m"), EWeatherState::Blizzard, 12.f, 1.f, 1.f, FarDistance, EKind::Still },
		{ TEXT("12_rain_night_2300"), EWeatherState::Snowfall, 23.f, 1.f, 0.7f, ViewDistance, EKind::Still },
		{ TEXT("13_hut_storm"), EWeatherState::Blizzard, 12.f, 1.f, 1.f, ViewDistance, EKind::Hut },
		{ TEXT("clip"), EWeatherState::Clear, 12.f, 0.f, 0.f, ViewDistance, EKind::Clip },
	};

	/** Mode "rainprobe" (look tuning): the storm with drop look variants (Probe = the variant index in Hour). */
	const FShot ProbeShots[] = {
		{ TEXT("probe_0_da"), EWeatherState::Blizzard, 0.f, 1.f, 1.f, ViewDistance, EKind::Probe },
		{ TEXT("probe_1_wide"), EWeatherState::Blizzard, 1.f, 1.f, 1.f, ViewDistance, EKind::Probe },
		{ TEXT("probe_2_bright"), EWeatherState::Blizzard, 2.f, 1.f, 1.f, ViewDistance, EKind::Probe },
		{ TEXT("probe_3_wide_bright"), EWeatherState::Blizzard, 3.f, 1.f, 1.f, ViewDistance, EKind::Probe },
		{ TEXT("probe_4_swapped"), EWeatherState::Blizzard, 4.f, 1.f, 1.f, ViewDistance, EKind::Probe },
		{ TEXT("probe_5_rain_da"), EWeatherState::Snowfall, 0.f, 1.f, 0.6f, ViewDistance, EKind::Probe },
		{ TEXT("probe_6_drops_x3_tiling04"), EWeatherState::Snowfall, 6.f, 1.f, 0.6f, ViewDistance, EKind::Probe },
		{ TEXT("probe_7_drops_x3_tiling10"), EWeatherState::Snowfall, 7.f, 1.f, 0.6f, ViewDistance, EKind::Probe },
	};

	/**
	 * Mode "rainterrain" (on /Game/Environment/Weather/Test/L_SnowTerrainTest, an arctic map switched to temperate by Codex.Env.Climate):
	 * the landscape's MF_RainWetSurface (wet darkening, puddles in the flat spots, ripples) - the leader stays where he spawned.
	 */
	const FShot TerrainShots[] = {
		{ TEXT("t1_terrain_dry"), EWeatherState::Overcast, 12.f, 0.f, 0.f, ViewDistance, EKind::Still },
		{ TEXT("t2_terrain_rain_wet050"), EWeatherState::Snowfall, 12.f, 0.5f, 0.f, ViewDistance, EKind::Still },
		{ TEXT("t3_terrain_rain_wet100_puddles100"), EWeatherState::Snowfall, 12.f, 1.f, 1.f, ViewDistance, EKind::Still },
		{ TEXT("t4_terrain_storm"), EWeatherState::Blizzard, 12.f, 1.f, 1.f, ViewDistance, EKind::Still },
		{ TEXT("t5_terrain_rain_close_15m"), EWeatherState::Snowfall, 12.f, 1.f, 1.f, CloseDistance, EKind::Still },
	};

	/** The probe variants (look tuning only). */
	FWeatherRainFxTuning ProbeLook(FWeatherRainFxTuning Look, int32 Variant)
	{
		if (Variant == 1 || Variant == 3)
		{
			Look.DropWidthMin *= 2.5f;
			Look.DropWidthMax *= 2.5f;
		}
		if (Variant == 2 || Variant == 3)
		{
			Look.OpacityMin = Look.OpacityMax = 1.f;
			Look.DropBrightness *= 2.f;
		}
		if (Variant == 6 || Variant == 7)
		{
			Look.ScreenDropsStrength *= 3.f;
			Look.ScreenDropsTiling = Variant == 6 ? 0.4f : 1.f;
		}
		if (Variant == 4)
		{
			// The size axes swapped (checks which sprite axis follows the velocity).
			Swap(Look.DropWidthMin, Look.DropLengthMin);
			Swap(Look.DropWidthMax, Look.DropLengthMax);
		}
		return Look;
	}

	/** The clip: 300 frames at -fps=30 (10 s), one capture every 2 (15 fps). */
	constexpr int32 ClipFrames = 300;
	constexpr int32 ClipEvery = 2;

	struct FPerfCase
	{
		const TCHAR* Name;
		EWeatherState Weather;
		float Wetness;
		float Puddles;
		bool bFx;
		bool bOverlay;
	};

	const FPerfCase PerfCases[] = {
		{ TEXT("sunny"), EWeatherState::Clear, 0.f, 0.f, true, true },
		{ TEXT("rain"), EWeatherState::Snowfall, 1.f, 0.5f, true, true },
		{ TEXT("storm"), EWeatherState::Blizzard, 1.f, 1.f, true, true },
		{ TEXT("storm_fxoff"), EWeatherState::Blizzard, 1.f, 1.f, false, true },
		{ TEXT("storm_nooverlay"), EWeatherState::Blizzard, 1.f, 1.f, true, false },
	};
	/** Two interleaved rounds per distance (the PC is shared: the medians of the rounds are compared, docs section 18.9). */
	const float PerfDistances[] = { ViewDistance, ViewDistance, FarDistance, FarDistance };
	constexpr int32 PerfSettleFrames = 360;
	constexpr int32 PerfMeasureFrames = 180;

	enum class EStep : uint8 { Warmup, Setup, Settle, Shoot, Clip, Measure, Write, Done };

	struct FRunner
	{
		EStep Step = EStep::Warmup;
		bool bPerf = false;
		bool bProbe = false;
		bool bTerrain = false;
		bool bClimateSwitched = false;
		bool bClip = true;
		int32 Frame = 0;
		int32 StepFrame = 0;
		int32 Index = 0;
		int32 ClipCount = 0;
		int32 SettleFrames = 180;
		int32 ExtraWait = 0;
		float ViewYaw = 45.f;
		double Gpu = 0., Render = 0., Game = 0., Draws = 0., Prims = 0.;
		int32 Samples = 0;
		TArray<float> GpuFrames;
		TArray<FString> Report;
		FString Dir;
		FString Label;
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

	void SetCVar(const TCHAR* Name, const TCHAR* Value)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Variable->Set(Value, ECVF_SetByConsole);
		}
	}

	void Capture(const FString& Path)
	{
		FScreenshotRequest::RequestScreenshot(Path, /*bInShowUI*/ true, /*bAddFilenameSuffix*/ false);
	}

	void OnDraw(UCanvas* Canvas, APlayerController*)
	{
		UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;
		if (!Runner.IsValid() || !Canvas || !Font || Runner->Label.IsEmpty())
		{
			return;
		}
		const float Scale = Canvas->SizeX / 1920.f * 1.25f;
		float W = 0.f, H = 0.f;
		Canvas->TextSize(Font, Runner->Label, W, H, Scale, Scale);
		FCanvasTileItem Tile(FVector2D(12.f, 12.f), FVector2D(W + 24.f, H + 14.f), FLinearColor(0.f, 0.f, 0.f, 0.55f));
		Tile.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Tile);
		FCanvasTextItem Text(FVector2D(24.f, 19.f), FText::FromString(Runner->Label), Font, FLinearColor(1.f, 0.92f, 0.4f));
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

	void PlaceLeader(UWorld* World, const FVector& Spot)
	{
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (Leader && FVector::DistSquared2D(Leader->GetActorLocation(), Spot) > FMath::Square(60.))
		{
			Leader->SetActorLocation(Spot, false, nullptr, ETeleportType::TeleportPhysics);
		}
	}

	/** The tactical camera's numbers on the leader (own camera actor). */
	void ViewLeader(UWorld* World, float Distance)
	{
		APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		if (!PC || !Squad || !Squad->GetLeader())
		{
			return;
		}
		if (!Runner->Camera.IsValid())
		{
			Runner->Camera = World->SpawnActor<ACameraActor>(FVector::ZeroVector, FRotator::ZeroRotator);
		}
		ACameraActor* Camera = Runner->Camera.Get();
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

	void UpdateLabel(UEnvironmentSubsystem& Env, const UWeatherRainFxComponent* Rain, const TCHAR* Name, float Distance)
	{
		Runner->Label = FString::Printf(TEXT("%s - %s %05.2f - wetness %.2f puddles %.2f - rain look %.2f - %d drops - screen drops %.2f - %.0f m"), Name,
			*Env.GetWeatherDisplayName(), Env.GetHour(), Env.GetWetness(), Env.GetPuddleAmount(), Rain ? Rain->GetCurrentIntensity() : 0.f,
			Rain ? Rain->GetParticleCount() : 0, Rain ? Rain->GetLastScreenDropsStrength() : 0.f, Distance / 100.f);
	}

	void Finish()
	{
		SetCVar(TEXT("Codex.Env.Lightning"), TEXT("-1"));
		SetCVar(TEXT("Codex.Env.SnowOverlay"), TEXT("1"));
		SetCVar(TEXT("Codex.Env.SnowGround"), TEXT("1"));
		for (const FString& Line : Runner->Report)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[RainPerf] %s"), *Line);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("EnvironmentShots %s: done (%d clip frames) -> %s"), Runner->bPerf ? TEXT("rainperf") : TEXT("rain"),
			Runner->ClipCount, *Runner->Dir);
		FCoreDelegates::OnEndFrame.Remove(Runner->EndFrameHandle);
		UDebugDrawService::Unregister(Runner->DrawHandle);
		FPlatformMisc::RequestExit(false, TEXT("EnvironmentShots"));
	}

	/** Weather (instant), hour, wetness / puddles; the clock stopped. Not a state jump: the rain fades in like in play. */
	void SetConditions(UEnvironmentSubsystem& Env, EWeatherState Weather, float Hour, float Wetness, float Puddles)
	{
		Env.SetClockScale(0.f);
		if (Env.GetWeather() != Weather || Env.GetNextWeather() != Weather)
		{
			Env.ForceWeather(Weather, true);
		}
		if (!FMath::IsNearlyEqual(Env.GetHour(), Hour, 0.01f))
		{
			Env.SetHour(Hour);
		}
		Env.SetWetness(Wetness, Puddles);
	}

	/** The clip: SunnyDay 2 s, the rain starts (fades in), the ground wets over 6 s, puddles from 5 s, storm + a strike at 8.5 / 9 s. */
	void DriveClip(UEnvironmentSubsystem& Env, UWeatherRainFxComponent* Rain, int32 Frame)
	{
		const float T = Frame / 30.f;
		const EWeatherState Weather = T < 2.f ? EWeatherState::Clear : T < 8.5f ? EWeatherState::Snowfall : EWeatherState::Blizzard;
		if (Env.GetWeather() != Weather)
		{
			Env.ForceWeather(Weather, true);
		}
		const float Wetness = FMath::Clamp((T - 2.f) / 6.f, 0.f, 1.f);
		const float Puddles = FMath::Clamp((T - 5.f) / 5.f, 0.f, 1.f) * 0.9f;
		Env.SetWetness(Wetness, Puddles);
		if (Rain && Frame == 270)
		{
			Rain->ForceStrike();
		}
	}

	void Tick()
	{
		FRunner& S = *Runner;
		++S.Frame;
		++S.StepFrame;
		UWorld* World = FrontendSmokeUtils::FindGameWorld();
		if (S.Frame > MaxFrames)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("EnvironmentShots rain: timeout in step %d"), static_cast<int32>(S.Step));
			Finish();
			return;
		}
		UEnvironmentSubsystem* Env = World ? UEnvironmentSubsystem::Get(World) : nullptr;
		USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr;
		if (!World || !Env || !Env->IsInitialised() || !Squad || !Squad->GetLeader())
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
		UWeatherRainFxComponent* Rain = Presenter ? Presenter->GetRainFx() : nullptr;
		const int32 ShotCount = S.bProbe ? static_cast<int32>(UE_ARRAY_COUNT(ProbeShots))
			: S.bTerrain ? static_cast<int32>(UE_ARRAY_COUNT(TerrainShots)) : static_cast<int32>(UE_ARRAY_COUNT(Shots));
		const FShot* ShotList = S.bProbe ? ProbeShots : S.bTerrain ? TerrainShots : Shots;
		const int32 PerfCount = static_cast<int32>(UE_ARRAY_COUNT(PerfCases)) * static_cast<int32>(UE_ARRAY_COUNT(PerfDistances));
		const int32 Count = S.bPerf ? PerfCount : ShotCount;
		while (!S.bPerf && !S.bClip && S.Index < ShotCount && ShotList[S.Index].Kind == EKind::Clip)
		{
			++S.Index;
		}
		const FShot& Shot = ShotList[FMath::Min(S.Index, ShotCount - 1)];
		const FPerfCase& Case = PerfCases[FMath::Min(S.Index, PerfCount - 1) % UE_ARRAY_COUNT(PerfCases)];
		const float Distance = S.bPerf ? PerfDistances[FMath::Min(S.Index, PerfCount - 1) / UE_ARRAY_COUNT(PerfCases)] : Shot.Distance;
		const TCHAR* Name = S.bPerf ? Case.Name : Shot.Name;

		switch (S.Step)
		{
		case EStep::Warmup:
			if (S.bTerrain && !S.bClimateSwitched && S.StepFrame >= WarmupFrames / 2 && Presenter && Presenter->IsApplying())
			{
				// An arctic test map shown in the temperate climate (debug switch): the presenter creates its rain component.
				S.bClimateSwitched = true;
				GEngine->Exec(World, TEXT("Codex.Env.Climate temperate"));
			}
			if (S.StepFrame >= WarmupFrames && Presenter && Presenter->IsApplying() && ShadersPending() == 0)
			{
				if (!Rain)
				{
					UE_LOG(LogCodexTactics, Warning, TEXT("EnvironmentShots rain: no rain component (not a temperate level?) - %s"), *Env->Describe());
					Finish();
					return;
				}
				if (const APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
				{
					S.ViewYaw = PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraRotation().Yaw : S.ViewYaw;
				}
				SetCVar(TEXT("Codex.Env.Lightning"), TEXT("0"));
				UE_LOG(LogCodexTactics, Display, TEXT("EnvironmentShots rain: view yaw %.0f, look from %s"), S.ViewYaw,
					Rain->IsUsingSettingsAsset() ? TEXT("DA_RainFX") : TEXT("C++ defaults"));
				S.Step = EStep::Setup;
				S.StepFrame = 0;
			}
			break;

		case EStep::Setup:
			if (S.Index >= Count)
			{
				S.Step = EStep::Done;
				break;
			}
			if (!S.bTerrain)
			{
				PlaceLeader(World, !S.bPerf && Shot.Kind == EKind::Hut ? HutSpot : LeaderSpot);
			}
			if (S.bPerf)
			{
				SetConditions(*Env, Case.Weather, 12.f, Case.Wetness, Case.Puddles);
				Rain->SetHiddenForCapture(!Case.bFx);
				FWeatherRainFxTuning Look = Rain->GetActiveTuning();
				Look.bScreenDrops = Case.bFx;
				Rain->SetTuningOverride(Case.bFx ? TOptional<FWeatherRainFxTuning>() : TOptional<FWeatherRainFxTuning>(Look));
				SetCVar(TEXT("Codex.Env.SnowOverlay"), Case.bOverlay ? TEXT("1") : TEXT("0"));
				SetCVar(TEXT("Codex.Env.SnowGround"), Case.bOverlay ? TEXT("1") : TEXT("0"));
				S.SettleFrames = PerfSettleFrames;
				S.Gpu = S.Render = S.Game = S.Draws = S.Prims = 0.;
				S.Samples = 0;
				S.GpuFrames.Reset();
			}
			else if (Shot.Kind == EKind::Clip)
			{
				SetConditions(*Env, EWeatherState::Clear, 12.f, 0.f, 0.f);
				Rain->SetTuningOverride(TOptional<FWeatherRainFxTuning>());
				S.SettleFrames = 150;
			}
			else if (Shot.Kind == EKind::Probe)
			{
				SetConditions(*Env, Shot.Weather, 12.f, Shot.Wetness, Shot.Puddles);
				Rain->SetTuningOverride(TOptional<FWeatherRainFxTuning>());
				Rain->SetTuningOverride(ProbeLook(Rain->GetActiveTuning(), static_cast<int32>(Shot.Hour)));
				S.SettleFrames = 150;
			}
			else
			{
				SetConditions(*Env, Shot.Weather, Shot.Hour, Shot.Wetness, Shot.Puddles);
				if (Shot.Kind == EKind::DropsBoost)
				{
					FWeatherRainFxTuning Look = Rain->GetActiveTuning();
					Look.ScreenDropsStrength *= 3.f;
					Rain->SetTuningOverride(Look);
				}
				else
				{
					Rain->SetTuningOverride(TOptional<FWeatherRainFxTuning>());
				}
				// The first shot of a new weather needs the full fade-in + drop fill; a wetness change only the overlay re-scan.
				S.SettleFrames = 180;
			}
			ViewLeader(World, Distance);
			S.ExtraWait = 0;
			S.Step = EStep::Settle;
			S.StepFrame = 0;
			break;

		case EStep::Settle:
			ViewLeader(World, Distance);
			UpdateLabel(*Env, Rain, Name, Distance);
			// A rainy shot waits until the drops fill the air (the first rain compiles the Niagara GPU shaders in an editor build).
			if (S.StepFrame >= S.SettleFrames && Rain->GetTargetIntensity() > 0.f && Rain->GetParticleCount() < 2000 && S.ExtraWait < S.SettleFrames * 5)
			{
				++S.ExtraWait;
				break;
			}
			if (S.StepFrame >= S.SettleFrames && ShadersPending() == 0)
			{
				S.StepFrame = 0;
				if (S.bPerf)
				{
					S.Step = EStep::Measure;
				}
				else if (Shot.Kind == EKind::Clip)
				{
					S.Step = EStep::Clip;
				}
				else
				{
					if (Shot.Kind == EKind::Strike)
					{
						Rain->ForceStrike();
					}
					S.Step = EStep::Shoot;
				}
			}
			break;

		case EStep::Shoot:
			ViewLeader(World, Distance);
			UpdateLabel(*Env, Rain, Name, Distance);
			if (S.StepFrame == 1)
			{
				Capture(S.Dir / (FString(Name) + TEXT(".png")));
				UE_LOG(LogCodexTactics, Display, TEXT("EnvironmentShots rain: %s (%s; flash %.2f; kill boxes %d; %s)"), Name, *Env->Describe(), Rain->GetFlashScale(),
					Rain->GetKillBoxes().Num(), *Rain->DescribeParticles());
				S.Step = EStep::Write;
				S.StepFrame = 0;
			}
			break;

		case EStep::Clip:
			DriveClip(*Env, Rain, S.StepFrame);
			ViewLeader(World, Distance);
			UpdateLabel(*Env, Rain, TEXT("rain clip"), Distance);
			if (S.StepFrame % ClipEvery == 0 && S.StepFrame <= ClipFrames)
			{
				Capture(S.Dir / TEXT("clip") / FString::Printf(TEXT("frame_%04d.png"), S.ClipCount++));
			}
			if (S.StepFrame > ClipFrames + 3)
			{
				S.Step = EStep::Write;
				S.StepFrame = 0;
			}
			break;

		case EStep::Measure:
			ViewLeader(World, Distance);
			UpdateLabel(*Env, Rain, Name, Distance);
			S.Gpu += FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0));
			S.GpuFrames.Add(static_cast<float>(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0))));
			S.Render += FPlatformTime::ToMilliseconds(GRenderThreadTime);
			S.Game += FPlatformTime::ToMilliseconds(GGameThreadTime);
			S.Draws += GNumDrawCallsRHI[0];
			S.Prims += GNumPrimitivesDrawnRHI[0];
			++S.Samples;
			if (S.StepFrame >= PerfMeasureFrames)
			{
				const double N = FMath::Max(S.Samples, 1);
				S.GpuFrames.Sort();
				const float Median = S.GpuFrames.Num() ? S.GpuFrames[S.GpuFrames.Num() / 2] : 0.f;
				const float P10 = S.GpuFrames.Num() ? S.GpuFrames[S.GpuFrames.Num() / 10] : 0.f;
				S.Report.Add(FString::Printf(TEXT("%s | camera %.0f m | round %d | GPU median %.2f ms p10 %.2f mean %.2f | render %.2f ms | game %.2f ms | draws %.0f | prims %.0f | drops %d | overlay %d obj %d ground"),
					Name, Distance / 100.f, 1 + (S.Index / static_cast<int32>(UE_ARRAY_COUNT(PerfCases))) % 2, Median, P10, S.Gpu / N, S.Render / N, S.Game / N, S.Draws / N, S.Prims / N, Rain->GetParticleCount(),
					Presenter->GetSnowOverlay() ? Presenter->GetSnowOverlay()->GetObjectTargetCount() : -1,
					Presenter->GetSnowOverlay() ? Presenter->GetSnowOverlay()->GetGroundTargetCount() : -1));
				Capture(S.Dir / FString::Printf(TEXT("%s%s.png"), Name, Distance > ViewDistance ? TEXT("_far") : TEXT("")));
				S.Step = EStep::Write;
				S.StepFrame = 0;
			}
			break;

		case EStep::Write:
			ViewLeader(World, Distance);
			if (S.StepFrame >= 10)
			{
				Rain->SetHiddenForCapture(false);
				++S.Index;
				S.Step = EStep::Setup;
				S.StepFrame = 0;
			}
			break;

		case EStep::Done:
			if (S.StepFrame >= 10)
			{
				Rain->SetTuningOverride(TOptional<FWeatherRainFxTuning>());
				Finish();
			}
			break;
		}
	}

	void Run(const TArray<FString>& Args)
	{
		Runner = MakeShared<FRunner>();
		Runner->bPerf = Args.Num() > 0 && Args[0].Equals(TEXT("rainperf"), ESearchCase::IgnoreCase);
		Runner->bProbe = Args.Num() > 0 && Args[0].Equals(TEXT("rainprobe"), ESearchCase::IgnoreCase);
		Runner->bTerrain = Args.Num() > 0 && Args[0].Equals(TEXT("rainterrain"), ESearchCase::IgnoreCase);
		Runner->bClip = !Args.ContainsByPredicate([](const FString& Arg) { return Arg.Equals(TEXT("noclip"), ESearchCase::IgnoreCase); });
		Runner->Dir = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir() / TEXT("..") / TEXT("Environment") / (Runner->bPerf ? TEXT("rainperf") : Runner->bProbe ? TEXT("rainprobe")
			: Runner->bTerrain ? TEXT("rain/terrain") : TEXT("rain")));
		FPaths::CollapseRelativeDirectories(Runner->Dir);
		if (!Runner->bPerf)
		{
			IFileManager::Get().DeleteDirectory(*(Runner->Dir / TEXT("clip")), false, true);
		}
		IFileManager::Get().MakeDirectory(*Runner->Dir, true);
		Runner->DrawHandle = UDebugDrawService::Register(TEXT("Game"), FDebugDrawDelegate::CreateStatic(&OnDraw));
		Runner->EndFrameHandle = FCoreDelegates::OnEndFrame.AddStatic(&Tick);
		UE_LOG(LogCodexTactics, Display, TEXT("EnvironmentShots %s -> %s"), Runner->bPerf ? TEXT("rainperf") : TEXT("rain"), *Runner->Dir);
	}
}

#endif // !UE_BUILD_SHIPPING
