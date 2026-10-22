// Dev-only console command: rendered captures of the environment system (day clock, procedural weather, cold, enemy roster) for a
// remote review (user on a phone, environment phase 2, 2026-10-22). Needs rendering (no -nullrhi); run offscreen with a fixed frame
// time so the sequence plays back at real speed:
//   UnrealEditor.exe CodexTactics.uproject /Game/Maps/L_MovementTest -game -RenderOffscreen -ResX=1280 -ResY=720 -nosound
//     -benchmark -fps=30 -ExecCmds="CodexTactics.EnvironmentShots"
// Output (Saved/Screenshots/Environment/):
//   timelapse/frame_0000.png ...   the clock at x60 (a 20 min day in ~20 s) for ~25 s, one frame per 3 (10 fps); a big overlay shows the
//                                  hour + phase, the weather -> next weather (blend), the commander's open-air cold rate with its
//                                  multipliers and the enemy roster for wave 2 under the current conditions (encode with ffmpeg)
//   wave_clear_midday.png          wave 2 started under Clear at 12:00 (roster as written), wide shot over the enemies
//   wave_blizzard_night.png        wave 2 started under Blizzard at 23:00 (no marksmen, frostbitten, more hounds), wide shot
// The overlay is drawn on the debug canvas (UDebugDrawService "Game"), enemy tags (M marksman, F frostbitten, H hound, ...) are
// projected by it too. The game is driven only from inside the engine; the squad is made safe so nothing disturbs the shots.
//
// Mode "visuals" (environment phase 3, the weather presenter - docs/systems/environment-weather.md section 11):
//   -ExecCmds="CodexTactics.EnvironmentShots visuals"            on the map it was started on (the lake: add -LevelJson=L_Demo01_A_FrozenLake.json)
//   -ExecCmds="CodexTactics.EnvironmentShots visuals timelapse"  the same + a full day at x60 (unlocks a locked level for it)
// Output (Saved/Screenshots/Environment/visuals/): <map>_<weather>_<hhmm>.png from ONE tactical camera view on the squad leader for
// Clear 07:00, Clear 12:00, Overcast 15:00, Snowfall 17:30, Blizzard 12:00, Clear 23:00, Snowfall 23:00 (a small label only, the game
// UI removed); timelapse_<map>/frame_NNNN.png at 10 fps (encode: ffmpeg -y -framerate 10 -i frame_%04d.png -vf scale=960:-2 -c:v libx264
// -pix_fmt yuv420p -crf 26 out.mp4).
//
// Mode "snowperf" (falling snow cost): -ExecCmds="CodexTactics.EnvironmentShots snowperf" -> log lines "[SnowFxPerf]" (GPU / render / game ms, draws)
// and stills Saved/Screenshots/Environment/snowperf/<map>_<state>[_far].png for snow FX off / Snowfall / Blizzard at the default and the max zoom
// (MovementTest: wave 2 is started first).
//
// Mode "snowcover" (environment phase 4c, the gradual snow cover overlay - docs/systems/environment-weather.md section 12):
//   -ExecCmds="CodexTactics.EnvironmentShots snowcover [nomovie] [noperf]"   on the map it was started on (the lake: add -LevelJson=...)
// Output (Saved/Screenshots/Environment/snowcover/): <map>_snow_000 / _035 / _070 / _100.png - Clear 12:00, a wide shot over the biggest
// object near the squad, SnowAmount forced through UEnvironmentSubsystem::SetSnowAmount; movie_<map>/frame_NNNN.png - SnowAmount 0 -> 1
// in 10 s at 15 fps (ffmpeg -y -framerate 15 -i frame_%04d.png -vf scale=960:-2 -c:v libx264 -pix_fmt yuv420p -crf 24 out.mp4); a perf
// A/B at SnowAmount 0.7 (Codex.Env.SnowOverlay 0 vs 1: GPU / render / game ms, RHI draws / primitives, log lines "[SnowPerf]") on the still
// view and in the level's largest wave when it has 13+ enemies (still <map>_wave<N>_snow_070.png). A presenter without an overlay
// component gets one from this command (which then writes MPC_Environment.SnowAmount / FrostAmount itself).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/TacticalCameraPawn.h"
#include "CanvasItem.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Core/CodexTacticsGameMode.h"
#include "Data/WaveConfigTypes.h"
#include "Debug/FrontendSmokeUtils.h"
#include "Debug/SmokeUtils.h"
#include "Debug/DebugDrawService.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Environment/WeatherPresenterActor.h"
#include "Environment/WeatherSnowFxComponent.h"
#include "Environment/WeatherSnowOverlayComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"
#include "RHI.h"
#include "RHIStats.h"
#include "UObject/UObjectIterator.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "Environment/EnvironmentRosterRules.h"
#include "Environment/EnvironmentRules.h"
#include "Environment/EnvironmentSubsystem.h"
#include "GameFramework/HUD.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "Survival/ColdRules.h"
#include "Survival/ColdSurvivalComponent.h"
#include "UnrealClient.h"

namespace EnvironmentShots
{
	constexpr int32 RosterWave = 2;
	/** Game seconds per real second of the clock in the timelapse: 1200 s day / 60 = 20 s. */
	constexpr float TimelapseClockScale = 60.f;
	constexpr int32 TimelapseFrames = 750; // 25 s at 30 fps
	constexpr int32 TimelapseEvery = 3;    // 10 fps video
	constexpr float TimelapseStartHour = 5.f;
	constexpr int32 TimelapseSeed = 11;
	constexpr int32 MaxFrames = 30 * 240;

	enum class EStage : uint8
	{
		WaitForGame,
		Timelapse,
		StillSetup,
		StillSettle,
		StillShot,
		StillWrite,
		Done
	};

	struct FOverlayLine
	{
		FString Text;
		FLinearColor Color = FLinearColor::White;
	};

	struct FRunner
	{
		EStage Stage = EStage::WaitForGame;
		int32 Frame = 0;
		int32 StageFrame = 0;
		int32 ClipCount = 0;
		int32 StillIndex = 0;
		FString Dir;
		FDelegateHandle EndFrameHandle;
		FDelegateHandle DrawHandle;
		TWeakObjectPtr<ACameraActor> Camera;
		TWeakObjectPtr<UWorld> World;
		TArray<FOverlayLine> Lines;
		bool bDayBar = false;
		bool bTags = false;
	};

	TSharedPtr<FRunner> Runner;

	struct FStillConditions
	{
		EWeatherState Weather;
		float Hour;
		const TCHAR* File;
	};

	const FStillConditions Stills[] = {
		{ EWeatherState::Clear, 12.f, TEXT("wave_clear_midday") },
		{ EWeatherState::Blizzard, 23.f, TEXT("wave_blizzard_night") },
	};

	FString TypeName(EEnemyArchetype Type)
	{
		switch (Type)
		{
		case EEnemyArchetype::FrostHound: return TEXT("HOUND");
		case EEnemyArchetype::Spitter: return TEXT("SPITTER");
		case EEnemyArchetype::Brute: return TEXT("BRUTE");
		case EEnemyArchetype::Frostbitten: return TEXT("FROSTBITTEN");
		case EEnemyArchetype::Cutter: return TEXT("CUTTER");
		case EEnemyArchetype::CryoDrone: return TEXT("DRONE");
		case EEnemyArchetype::Marksman: return TEXT("MARKSMAN");
		default: return TEXT("BASE");
		}
	}

	/** Tag letter + colour of an enemy type on the stills. */
	void TagOf(EEnemyArchetype Type, FString& OutLetter, FLinearColor& OutColor)
	{
		switch (Type)
		{
		case EEnemyArchetype::Marksman: OutLetter = TEXT("M"); OutColor = FLinearColor(1.f, 0.15f, 0.15f); break;
		case EEnemyArchetype::Frostbitten: OutLetter = TEXT("F"); OutColor = FLinearColor(0.2f, 0.9f, 1.f); break;
		case EEnemyArchetype::FrostHound: OutLetter = TEXT("H"); OutColor = FLinearColor(1.f, 0.9f, 0.1f); break;
		case EEnemyArchetype::Cutter: OutLetter = TEXT("C"); OutColor = FLinearColor(1.f, 0.55f, 0.1f); break;
		case EEnemyArchetype::Spitter: OutLetter = TEXT("S"); OutColor = FLinearColor(0.4f, 1.f, 0.3f); break;
		default: OutLetter = TEXT("?"); OutColor = FLinearColor::White; break;
		}
	}

	FString HourText(float Hour)
	{
		const int32 Total = FMath::FloorToInt(FMath::Fmod(Hour, 24.f) * 60.f);
		return FString::Printf(TEXT("%02d:%02d"), Total / 60, Total % 60);
	}

	/** The commander's open-air cold rate (standing, from 0 %, no heat / preparation / sprint) under the conditions in force. */
	float CommanderColdRate(UWorld* World, float& OutWeatherMult, float& OutPhaseMult)
	{
		OutWeatherMult = 1.f;
		OutPhaseMult = 1.f;
		USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr;
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		UColdSurvivalComponent* Cold = Leader ? Leader->ColdSurvival.Get() : nullptr;
		if (!Cold)
		{
			return 0.f;
		}
		FColdEnvironment Open = Cold->GatherEnvironment();
		Open.bWarm = false;
		Open.bPreparation = false;
		Open.bSprinting = false;
		OutWeatherMult = Open.WeatherMultiplier;
		OutPhaseMult = Open.PhaseMultiplier;
		return ColdRules::StepCold(Cold->Config, 0.f, 1.f, Open, EOperativeStance::Standing, Cold->Fortitude);
	}

	/** "MARKSMAN -> FROSTBITTEN half, HOUND x1.5" for the conditions (what EnvironmentRosterRules::ResolveSpawns will do to a wave). */
	FString RosterSummary(const FEnvironmentConfig& Config, EWeatherState Weather, ETimeOfDayPhase Phase)
	{
		const FEnvironmentRosterConfig& Roster = Config.Roster;
		TArray<FString> Parts;
		if (Roster.bEnabled)
		{
			for (const FEnvironmentRosterPair& Pair : Roster.Pairs)
			{
				float Score = 0.f;
				if (EnvironmentRosterRules::GetSuitability(Roster, Pair.From, Weather, Phase, Score))
				{
					const float Share = EnvironmentRosterRules::GetSwapShare(Roster, Score);
					if (Share > 0.f)
					{
						Parts.Add(FString::Printf(TEXT("%s -> %s %s"), *TypeName(Pair.From), *TypeName(Pair.To), Share >= 1.f ? TEXT("all") : TEXT("half")));
					}
				}
			}
			for (const EEnemyArchetype Type : Roster.BonusTypes)
			{
				float Score = 0.f;
				if (EnvironmentRosterRules::GetSuitability(Roster, Type, Weather, Phase, Score) && Score > Roster.BonusAbove)
				{
					Parts.Add(FString::Printf(TEXT("%s x%.1f"), *TypeName(Type), Roster.BonusMultiplier));
				}
			}
		}
		return Parts.IsEmpty() ? FString(TEXT("as written")) : FString::Join(Parts, TEXT(", "));
	}

	/** Wave RosterWave of the level resolved for the conditions: the enemy total plus a "TYPE n" list. */
	void WaveRoster(const UWorld* World, const FEnvironmentConfig& Config, EWeatherState Weather, ETimeOfDayPhase Phase, int32& OutTotal, FString& OutList)
	{
		OutTotal = 0;
		OutList.Reset();
		const ACodexTacticsGameMode* GameMode = World ? World->GetAuthGameMode<ACodexTacticsGameMode>() : nullptr;
		const ULevelConfigAsset* Level = GameMode ? GameMode->GetActiveLevelConfig() : nullptr;
		if (!Level || !Level->Config.Waves.IsValidIndex(RosterWave - 1))
		{
			return;
		}
		const TArray<FEnemySpawnEntry> Resolved = EnvironmentRosterRules::ResolveSpawns(Config.Roster, Level->Config.Waves[RosterWave - 1].Spawns, Weather, Phase);
		TMap<EEnemyArchetype, int32> Counts;
		for (const FEnemySpawnEntry& Entry : Resolved)
		{
			Counts.FindOrAdd(Entry.EnemyType) += Entry.Count;
			OutTotal += Entry.Count;
		}
		for (const TPair<EEnemyArchetype, int32>& Pair : Counts)
		{
			OutList += FString::Printf(TEXT("%s%s %d"), OutList.IsEmpty() ? TEXT("") : TEXT(", "), *TypeName(Pair.Key), Pair.Value);
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
	}

	/** Clean captures: no game HUD text / nameplates (the overlay is drawn by this command); the tactical camera on the squad leader. */
	void FocusOnSquad(UWorld* World)
	{
		APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		if (!PC || !Squad || !Squad->GetLeader())
		{
			return;
		}
		if (AHUD* Hud = PC->GetHUD())
		{
			Hud->bShowHUD = false;
		}
		if (ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(PC->GetPawn()))
		{
			PC->SetViewTarget(Camera);
			Camera->SnapFocusOnTarget(Squad->GetLeader());
		}
	}

	void UpdateTimelapseOverlay(UWorld* World, UEnvironmentSubsystem& Environment)
	{
		FRunner& R = *Runner;
		const FEnvironmentConfig& Config = Environment.GetConfig();
		const ETimeOfDayPhase Phase = Environment.GetPhase();
		const EWeatherState Weather = Environment.GetWeather();
		const EWeatherState Next = Environment.GetNextWeather();
		const float Blend = Environment.GetTransitionBlend();
		float WeatherMult = 1.f, PhaseMult = 1.f;
		const float Rate = CommanderColdRate(World, WeatherMult, PhaseMult);
		int32 Total = 0;
		FString List;
		WaveRoster(World, Config, Weather, Phase, Total, List);

		R.Lines.Reset();
		R.Lines.Add({ FString::Printf(TEXT("Hour %s - %s   (clock x%.0f)"), *HourText(Environment.GetHour()), *EnvironmentRules::PhaseToString(Phase), Environment.GetClockScale()),
			FLinearColor(1.f, 0.9f, 0.3f) });
		R.Lines.Add({ FString::Printf(TEXT("Weather %s -> %s (blend %.2f)"), *EnvironmentRules::WeatherToString(Weather), *EnvironmentRules::WeatherToString(Next), Blend),
			FLinearColor(0.5f, 0.9f, 1.f) });
		R.Lines.Add({ FString::Printf(TEXT("Cold rate commander: %.2f %%/s (x weather %.2f - phase %.2f)"), Rate, WeatherMult, PhaseMult),
			FLinearColor(1.f, 0.6f, 0.35f) });
		R.Lines.Add({ FString::Printf(TEXT("Roster now: %s"), *RosterSummary(Config, Weather, Phase)), FLinearColor(0.6f, 1.f, 0.6f) });
		R.Lines.Add({ FString::Printf(TEXT("Wave %d would be %d enemies: %s"), RosterWave, Total, *List), FLinearColor(0.85f, 0.85f, 0.85f) });
	}

	/** Draws a filled rectangle on the canvas. */
	void Box(UCanvas* Canvas, float X, float Y, float W, float H, const FLinearColor& Color)
	{
		FCanvasTileItem Tile(FVector2D(X, Y), FVector2D(W, H), Color);
		Tile.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Tile);
	}

	void DrawText(UCanvas* Canvas, UFont* Font, const FString& Text, float X, float Y, float Scale, const FLinearColor& Color)
	{
		FCanvasTextItem Item(FVector2D(X, Y), FText::FromString(Text), Font, Color);
		Item.Scale = FVector2D(Scale, Scale);
		Item.EnableShadow(FLinearColor(0.f, 0.f, 0.f, 0.9f), FVector2D(1.5f, 1.5f));
		Canvas->DrawItem(Item);
	}

	void DrawDayBar(UCanvas* Canvas, UFont* Font, const FEnvironmentConfig& Config, float Hour, EWeatherState Weather, EWeatherState Next, float Blend,
		float X, float Y, float W, float Scale)
	{
		const float BarH = 18.f * Scale;
		// 0..24 h in four phase colours (Night wraps over midnight).
		struct FSegment { float From; float To; FLinearColor Color; };
		const FSegment Segments[] = {
			{ 0.f, Config.EarlyMorningStartHour, FLinearColor(0.10f, 0.12f, 0.35f) },
			{ Config.EarlyMorningStartHour, Config.MiddayStartHour, FLinearColor(0.85f, 0.55f, 0.35f) },
			{ Config.MiddayStartHour, Config.AfternoonStartHour, FLinearColor(0.95f, 0.9f, 0.55f) },
			{ Config.AfternoonStartHour, Config.NightStartHour, FLinearColor(0.9f, 0.55f, 0.2f) },
			{ Config.NightStartHour, 24.f, FLinearColor(0.10f, 0.12f, 0.35f) },
		};
		for (const FSegment& Segment : Segments)
		{
			Box(Canvas, X + W * Segment.From / 24.f, Y, W * (Segment.To - Segment.From) / 24.f, BarH, Segment.Color);
		}
		const float MarkerX = X + W * FMath::Fmod(Hour, 24.f) / 24.f;
		Box(Canvas, MarkerX - 3.f, Y - 5.f, 6.f, BarH + 10.f, FLinearColor::White);

		// The weather ladder: the cell in force is bright, the one it blends into fills with the transition.
		const float LadderY = Y + BarH + 12.f * Scale;
		const float CellW = W / 4.f;
		const EWeatherState Ladder[] = { EWeatherState::Clear, EWeatherState::Overcast, EWeatherState::Snowfall, EWeatherState::Blizzard };
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const float CellX = X + CellW * Index;
			Box(Canvas, CellX + 2.f, LadderY, CellW - 4.f, BarH * 1.6f, FLinearColor(0.f, 0.f, 0.f, 0.55f));
			if (Ladder[Index] == Weather)
			{
				Box(Canvas, CellX + 2.f, LadderY, (CellW - 4.f) * (1.f - Blend), BarH * 1.6f, FLinearColor(0.35f, 0.75f, 1.f, 0.95f));
			}
			if (Ladder[Index] == Next && Next != Weather)
			{
				Box(Canvas, CellX + 2.f, LadderY, (CellW - 4.f) * Blend, BarH * 1.6f, FLinearColor(1.f, 0.85f, 0.3f, 0.95f));
			}
			DrawText(Canvas, Font, EnvironmentRules::WeatherToString(Ladder[Index]), CellX + 8.f, LadderY + 2.f * Scale, Scale * 0.8f, FLinearColor::White);
		}
	}

	/** Debug-canvas callback ("Game" channel): the overlay panel, the day bar and the enemy tags. */
	void OnDraw(UCanvas* Canvas, APlayerController* /*Controller*/)
	{
		if (!Runner.IsValid() || !Canvas || !GEngine)
		{
			return;
		}
		FRunner& R = *Runner;
		UFont* Font = GEngine->GetLargeFont();
		if (!Font)
		{
			return;
		}
		UWorld* World = R.World.Get();
		const float Margin = 16.f;
		const float PanelW = Canvas->SizeX - Margin * 2.f;
		float MaxWidth = 1.f;
		for (const FOverlayLine& Line : R.Lines)
		{
			float W = 0.f, H = 0.f;
			Canvas->TextSize(Font, Line.Text, W, H, 1.f, 1.f);
			MaxWidth = FMath::Max(MaxWidth, W);
		}
		const float Scale = FMath::Clamp((PanelW - 24.f) / MaxWidth, 0.4f, 1.15f);
		float LineH = 0.f, Dummy = 0.f;
		Canvas->TextSize(Font, TEXT("Hg"), Dummy, LineH, Scale, Scale);
		LineH *= 1.15f;

		const float PanelH = LineH * R.Lines.Num() + 16.f + (R.bDayBar ? 18.f * Scale * 2.6f + 26.f * Scale : 0.f);
		Box(Canvas, Margin, Margin, PanelW, PanelH, FLinearColor(0.f, 0.f, 0.f, 0.62f));
		float Y = Margin + 8.f;
		for (const FOverlayLine& Line : R.Lines)
		{
			DrawText(Canvas, Font, Line.Text, Margin + 12.f, Y, Scale, Line.Color);
			Y += LineH;
		}
		if (R.bDayBar && World)
		{
			if (const UEnvironmentSubsystem* Environment = UEnvironmentSubsystem::Get(World))
			{
				DrawDayBar(Canvas, Font, Environment->GetConfig(), Environment->GetHour(), Environment->GetWeather(), Environment->GetNextWeather(),
					Environment->GetTransitionBlend(), Margin + 12.f, Y + 8.f, PanelW - 24.f, Scale);
			}
		}

		if (R.bTags && World)
		{
			UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
			for (const AEnemyCharacter* Enemy : Waves ? Waves->GetAliveEnemies() : TArray<AEnemyCharacter*>())
			{
				if (!Enemy)
				{
					continue;
				}
				FString Letter;
				FLinearColor Color;
				TagOf(Enemy->GetArchetype(), Letter, Color);
				const FVector Screen = Canvas->Project(Enemy->GetActorLocation() + FVector(0.f, 0.f, 230.f));
				if (Screen.Z > 0.f && Screen.X > 0.f && Screen.X < Canvas->SizeX && Screen.Y > PanelH + Margin && Screen.Y < Canvas->SizeY)
				{
					const bool bMarksman = Enemy->GetArchetype() == EEnemyArchetype::Marksman;
					DrawText(Canvas, Font, Letter, Screen.X - 6.f, Screen.Y - 12.f, bMarksman ? 1.7f : 1.1f, Color);
				}
			}
		}
	}

	void Finish()
	{
		UE_LOG(LogCodexTactics, Display, TEXT("EnvironmentShots: done (%d timelapse frames) -> %s"), Runner->ClipCount, *Runner->Dir);
		FCoreDelegates::OnEndFrame.Remove(Runner->EndFrameHandle);
		UDebugDrawService::Unregister(Runner->DrawHandle);
		FPlatformMisc::RequestExit(false, TEXT("EnvironmentShots"));
	}

	void Capture(const FString& Path)
	{
		FScreenshotRequest::RequestScreenshot(Path, /*bInShowUI*/ true, /*bAddFilenameSuffix*/ false);
	}

	/** Wide shot over every living enemy, from the squad's side, high enough to see the spawn area. */
	void FrameEnemies(UWorld* World)
	{
		UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
		APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		if (!Waves || !PC || !Squad || !Squad->GetLeader())
		{
			return;
		}
		FBox Bounds(ForceInit);
		for (const AEnemyCharacter* Enemy : Waves->GetAliveEnemies())
		{
			if (Enemy)
			{
				Bounds += Enemy->GetActorLocation();
			}
		}
		if (!Bounds.IsValid)
		{
			return;
		}
		const FVector Centre = Bounds.GetCenter();
		const float Radius = FMath::Max(Bounds.GetExtent().Size2D(), 600.f);
		FVector Side = (Squad->GetLeader()->GetActorLocation() - Centre).GetSafeNormal2D();
		Side = Side.IsNearlyZero() ? FVector(-1.f, 0.f, 0.f) : Side;
		const FVector Eye = Centre + Side * (Radius * 1.2f + 300.f) + FVector(0.f, 0.f, Radius * 0.65f + 300.f);
		if (!Runner->Camera.IsValid())
		{
			Runner->Camera = World->SpawnActor<ACameraActor>(Eye, FRotator::ZeroRotator);
		}
		if (ACameraActor* Camera = Runner->Camera.Get())
		{
			Camera->SetActorLocationAndRotation(Eye, (Centre - Eye).Rotation());
			Camera->GetCameraComponent()->SetFieldOfView(70.f);
			Camera->GetCameraComponent()->bConstrainAspectRatio = false;
			PC->SetViewTarget(Camera);
		}
	}

	void BeginStill(UWorld* World, UEnvironmentSubsystem& Environment, UWaveSubsystem& Waves, const FStillConditions& Conditions)
	{
		Environment.SetClockScale(0.f);
		Environment.ForceWeather(Conditions.Weather, true);
		Environment.SetHour(Conditions.Hour);
		Waves.ClearAllEnemies();
		Waves.StartWave(RosterWave);
		UE_LOG(LogCodexTactics, Display, TEXT("EnvironmentShots: still %s - %s - wave %d: %d enemies"), Conditions.File, *Environment.Describe(), RosterWave,
			Waves.GetAliveEnemyCount());
	}

	void UpdateStillOverlay(UWorld* World, UEnvironmentSubsystem& Environment, UWaveSubsystem& Waves, const FStillConditions& Conditions)
	{
		FRunner& R = *Runner;
		TMap<EEnemyArchetype, int32> Alive;
		for (const AEnemyCharacter* Enemy : Waves.GetAliveEnemies())
		{
			if (Enemy)
			{
				Alive.FindOrAdd(Enemy->GetArchetype())++;
			}
		}
		FString List;
		for (const TPair<EEnemyArchetype, int32>& Pair : Alive)
		{
			List += FString::Printf(TEXT("%s%s %d"), List.IsEmpty() ? TEXT("") : TEXT(", "), *TypeName(Pair.Key), Pair.Value);
		}
		R.Lines.Reset();
		R.Lines.Add({ FString::Printf(TEXT("%s - %s - wave %d: %d enemies"), *EnvironmentRules::WeatherToString(Conditions.Weather), *HourText(Conditions.Hour), RosterWave,
			Waves.GetAliveEnemyCount()), FLinearColor(1.f, 0.9f, 0.3f) });
		R.Lines.Add({ List, FLinearColor(0.85f, 0.85f, 0.85f) });
		R.Lines.Add({ FString::Printf(TEXT("Roster: %s"), *RosterSummary(Environment.GetConfig(), Conditions.Weather, Environment.GetPhase())), FLinearColor(0.6f, 1.f, 0.6f) });
		R.Lines.Add({ TEXT("M marksman  F frostbitten  H hound  C cutter"), FLinearColor(0.7f, 0.7f, 0.7f) });
	}

	void Tick()
	{
		FRunner& R = *Runner;
		++R.Frame;
		++R.StageFrame;
		UWorld* World = FrontendSmokeUtils::FindGameWorld();
		if (R.Frame > MaxFrames)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("EnvironmentShots: timeout in stage %d"), static_cast<int32>(R.Stage));
			Finish();
			return;
		}
		if (!World)
		{
			return;
		}
		R.World = World;
		UEnvironmentSubsystem* Environment = UEnvironmentSubsystem::Get(World);
		UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		if (!Environment || !Waves || !Squad || !Squad->GetLeader() || !Environment->IsInitialised())
		{
			return;
		}
		KeepSquadSafe(World);

		switch (R.Stage)
		{
		case EStage::WaitForGame:
			if (R.StageFrame < 60)
			{
				break;
			}
			SmokeUtils::PlaceSquadAtTestStart(World);
			FocusOnSquad(World);
			Environment->RestartWeatherSequence(TimelapseSeed);
			Environment->ForceWeather(EWeatherState::Clear, true);
			Environment->SetHour(TimelapseStartHour);
			Environment->SetClockScale(TimelapseClockScale);
			R.bDayBar = true;
			R.Stage = EStage::Timelapse;
			R.StageFrame = 0;
			UE_LOG(LogCodexTactics, Display, TEXT("EnvironmentShots: timelapse start - %s"), *Environment->Describe());
			break;

		case EStage::Timelapse:
			UpdateTimelapseOverlay(World, *Environment);
			if (R.StageFrame % TimelapseEvery == 0 && R.StageFrame <= TimelapseFrames)
			{
				Capture(R.Dir / TEXT("timelapse") / FString::Printf(TEXT("frame_%04d.png"), R.ClipCount++));
			}
			if (R.StageFrame > TimelapseFrames + 3)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("EnvironmentShots: timelapse end - %s"), *Environment->Describe());
				R.bDayBar = false;
				R.Stage = EStage::StillSetup;
				R.StageFrame = 0;
			}
			break;

		case EStage::StillSetup:
			if (R.StageFrame == 1)
			{
				SmokeUtils::PlaceSquadAwayFromWaveSpawns(World, 3000.f);
				BeginStill(World, *Environment, *Waves, Stills[R.StillIndex]);
				R.bTags = true;
				R.Stage = EStage::StillSettle;
				R.StageFrame = 0;
			}
			break;

		case EStage::StillSettle:
			// A few frames of life, then the wave stops where it spawned so the wide shot is clean.
			UpdateStillOverlay(World, *Environment, *Waves, Stills[R.StillIndex]);
			FrameEnemies(World);
			if (R.StageFrame >= 20)
			{
				for (AEnemyCharacter* Enemy : Waves->GetAliveEnemies())
				{
					if (Enemy)
					{
						Enemy->CustomTimeDilation = 0.f;
					}
				}
				R.Stage = EStage::StillShot;
				R.StageFrame = 0;
			}
			break;

		case EStage::StillShot:
			UpdateStillOverlay(World, *Environment, *Waves, Stills[R.StillIndex]);
			FrameEnemies(World);
			if (R.StageFrame >= 10)
			{
				Capture(R.Dir / (FString(Stills[R.StillIndex].File) + TEXT(".png")));
				UE_LOG(LogCodexTactics, Display, TEXT("EnvironmentShots: still %s"), Stills[R.StillIndex].File);
				R.Stage = EStage::StillWrite;
				R.StageFrame = 0;
			}
			break;

		case EStage::StillWrite:
			if (R.StageFrame >= 15)
			{
				Waves->ClearAllEnemies();
				R.bTags = false;
				++R.StillIndex;
				R.Stage = R.StillIndex < UE_ARRAY_COUNT(Stills) ? EStage::StillSetup : EStage::Done;
				R.StageFrame = 0;
			}
			break;

		case EStage::Done:
			Finish();
			break;
		}
	}

	// --- Mode "visuals": the weather presenter's look (environment phase 3) ---

	namespace Visuals
	{
		constexpr int32 SettleFrames = 90;       // 3 s at -fps=30: eye adaptation settles
		constexpr int32 WarmupFrames = 150;      // the level start (fades, the presenter's minimap hold)
		constexpr int32 DayFrames = 630;         // a 1200 s day at x60 = 20 s = 600 frames, + margin
		constexpr float DayStartHour = 5.f;
		constexpr int32 DaySeed = 11;

		struct FShot
		{
			EWeatherState Weather;
			float Hour;
		};

		const FShot Shots[] = {
			{ EWeatherState::Clear, 7.f }, { EWeatherState::Clear, 12.f }, { EWeatherState::Overcast, 15.f }, { EWeatherState::Snowfall, 17.5f },
			{ EWeatherState::Blizzard, 12.f }, { EWeatherState::Clear, 23.f }, { EWeatherState::Snowfall, 23.f },
		};

		enum class EStep : uint8 { Warmup, Setup, Settle, Write, DaySetup, Day, Done };

		struct FVisualsRunner
		{
			EStep Step = EStep::Warmup;
			int32 Frame = 0;
			int32 StepFrame = 0;
			int32 ShotIndex = 0;
			int32 ClipCount = 0;
			bool bTimelapse = false;
			FString MapName;
			FString Dir;
			FDelegateHandle EndFrameHandle;
		};

		TSharedPtr<FVisualsRunner> VRunner;

		/** Camera yaw of the tactical view (taken from the game camera once), pitch and distance of the fixed still camera. */
		float ViewYaw = 45.f;
		constexpr float ViewPitch = -52.f;
		constexpr float ViewDistance = 2600.f;
		constexpr float ViewFov = 60.f;

		/**
		 * The same tactical view on the squad leader for every still: an own camera actor (a level script / tutorial camera can neither
		 * move nor replace it), re-aimed every frame so a walking squad stays framed.
		 */
		void ViewSquad(UWorld* World)
		{
			APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
			USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
			if (!PC || !Squad || !Squad->GetLeader())
			{
				return;
			}
			if (AHUD* Hud = PC->GetHUD())
			{
				Hud->bShowHUD = false;
			}
			const FVector Focus = Squad->GetLeader()->GetActorLocation();
			const FRotator Rotation(ViewPitch, ViewYaw, 0.f);
			const FVector Eye = Focus - Rotation.Vector() * ViewDistance;
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

		FString HourTag(float Hour)
		{
			const int32 Total = FMath::RoundToInt(FMath::Fmod(Hour, 24.f) * 60.f);
			return FString::Printf(TEXT("%02d%02d"), Total / 60, Total % 60);
		}

		void Label(UEnvironmentSubsystem& Environment, bool bWithBlend)
		{
			FRunner& R = *Runner;
			R.Lines.Reset();
			FString Text = FString::Printf(TEXT("%s - %s %s"), *VRunner->MapName, *EnvironmentRules::WeatherToString(Environment.GetWeather()),
				*HourText(Environment.GetHour()));
			if (bWithBlend && Environment.GetTransitionBlend() > 0.f)
			{
				Text += FString::Printf(TEXT(" -> %s %.0f%%"), *EnvironmentRules::WeatherToString(Environment.GetNextWeather()), Environment.GetTransitionBlend() * 100.f);
			}
			R.Lines.Add({ Text, FLinearColor(1.f, 0.9f, 0.3f) });
		}

		void Finish()
		{
			UE_LOG(LogCodexTactics, Display, TEXT("EnvironmentShots visuals: done (%d stills, %d timelapse frames) -> %s"), VRunner->ShotIndex,
				VRunner->ClipCount, *VRunner->Dir);
			FCoreDelegates::OnEndFrame.Remove(VRunner->EndFrameHandle);
			UDebugDrawService::Unregister(Runner->DrawHandle);
			FPlatformMisc::RequestExit(false, TEXT("EnvironmentShots"));
		}

		void Tick()
		{
			FVisualsRunner& V = *VRunner;
			++V.Frame;
			++V.StepFrame;
			UWorld* World = FrontendSmokeUtils::FindGameWorld();
			if (V.Frame > MaxFrames)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("EnvironmentShots visuals: timeout in step %d"), static_cast<int32>(V.Step));
				Finish();
				return;
			}
			UEnvironmentSubsystem* Environment = World ? UEnvironmentSubsystem::Get(World) : nullptr;
			USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr;
			if (!World || !Environment || !Environment->IsInitialised() || !Squad || !Squad->GetLeader())
			{
				return;
			}
			Runner->World = World;
			KeepSquadSafe(World);
			UWidgetLayoutLibrary::RemoveAllWidgets(World);
			const AWeatherPresenterActor* Presenter = AWeatherPresenterActor::Find(World);

			switch (V.Step)
			{
			case EStep::Warmup:
				if (V.StepFrame >= WarmupFrames && Presenter && Presenter->IsApplying())
				{
					V.MapName = World->GetMapName();
					V.MapName.RemoveFromStart(World->StreamingLevelsPrefix);
					if (V.MapName.Contains(TEXT("MovementTest")))
					{
						SmokeUtils::PlaceSquadAtTestStart(World);
					}
					if (const APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
					{
						ViewYaw = PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraRotation().Yaw : ViewYaw;
					}
					UE_LOG(LogCodexTactics, Display, TEXT("EnvironmentShots visuals: %s, view yaw %.0f, presenter: %s"), *V.MapName, ViewYaw,
						*Presenter->DescribeAdopted());
					V.Step = EStep::Setup;
					V.StepFrame = 0;
				}
				break;

			case EStep::Setup:
			{
				const FShot& Shot = Shots[V.ShotIndex];
				Environment->SetClockScale(0.f);
				Environment->ForceWeather(Shot.Weather, true);
				Environment->SetHour(Shot.Hour);
				ViewSquad(World);
				Label(*Environment, false);
				V.Step = EStep::Settle;
				V.StepFrame = 0;
				break;
			}

			case EStep::Settle:
				ViewSquad(World);
				if (V.StepFrame >= SettleFrames)
				{
					const FShot& Shot = Shots[V.ShotIndex];
					const FString File = FString::Printf(TEXT("%s_%s_%s.png"), *V.MapName, *EnvironmentRules::WeatherToString(Shot.Weather), *HourTag(Shot.Hour));
					Capture(V.Dir / File);
					UE_LOG(LogCodexTactics, Display, TEXT("EnvironmentShots visuals: %s (%s)"), *File, *Environment->Describe());
					V.Step = EStep::Write;
					V.StepFrame = 0;
				}
				break;

			case EStep::Write:
				if (V.StepFrame >= 10)
				{
					++V.ShotIndex;
					V.Step = V.ShotIndex < UE_ARRAY_COUNT(Shots) ? EStep::Setup : (V.bTimelapse ? EStep::DaySetup : EStep::Done);
					V.StepFrame = 0;
				}
				break;

			case EStep::DaySetup:
			{
				// A locked level (the lake) is unlocked for the timelapse only (this run exits afterwards).
				Environment->RestartWeatherSequence(DaySeed);
				FEnvironmentState State = Environment->GetState();
				State.bLocked = false;
				State.Hour = DayStartHour;
				Environment->SetState(State);
				Environment->ForceWeather(EWeatherState::Clear, true);
				Environment->SetClockScale(TimelapseClockScale);
				ViewSquad(World);
				UE_LOG(LogCodexTactics, Display, TEXT("EnvironmentShots visuals: timelapse start - %s"), *Environment->Describe());
				V.Step = EStep::Day;
				V.StepFrame = 0;
				break;
			}

			case EStep::Day:
				ViewSquad(World);
				Label(*Environment, true);
				if (V.StepFrame % TimelapseEvery == 0 && V.StepFrame <= DayFrames)
				{
					Capture(V.Dir / (TEXT("timelapse_") + V.MapName) / FString::Printf(TEXT("frame_%04d.png"), V.ClipCount++));
				}
				if (V.StepFrame > DayFrames + 3)
				{
					UE_LOG(LogCodexTactics, Display, TEXT("EnvironmentShots visuals: timelapse end - %s"), *Environment->Describe());
					V.Step = EStep::Done;
					V.StepFrame = 0;
				}
				break;

			case EStep::Done:
				if (V.StepFrame >= 10)
				{
					Finish();
				}
				break;
			}
		}

		void Run(bool bTimelapse)
		{
			Runner = MakeShared<FRunner>();
			VRunner = MakeShared<FVisualsRunner>();
			VRunner->bTimelapse = bTimelapse;
			VRunner->Dir = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir() / TEXT("..") / TEXT("Environment") / TEXT("visuals"));
			FPaths::CollapseRelativeDirectories(VRunner->Dir);
			IFileManager::Get().MakeDirectory(*VRunner->Dir, true);
			Runner->Dir = VRunner->Dir;
			Runner->DrawHandle = UDebugDrawService::Register(TEXT("Game"), FDebugDrawDelegate::CreateStatic(&OnDraw));
			VRunner->EndFrameHandle = FCoreDelegates::OnEndFrame.AddStatic(&Tick);
			UE_LOG(LogCodexTactics, Display, TEXT("EnvironmentShots visuals: %d stills%s -> %s"), static_cast<int32>(UE_ARRAY_COUNT(Shots)),
				bTimelapse ? TEXT(" + timelapse") : TEXT(""), *VRunner->Dir);
		}
	}

	// --- Mode "snow": the falling snow for the tactical camera (environment phase 4b, docs/systems/environment-weather.md section 12) ---
	//   -ExecCmds="CodexTactics.EnvironmentShots snow"   on the map it was started on (the lake: add -LevelJson=L_Demo01_A_FrozenLake.json)
	// Output (Saved/Screenshots/Environment/snow/): <map>_<weather>_<hhmm>[_far].png - Snowfall 12:00, Blizzard 12:00 (+ max zoom), Snowfall
	// 23:00, Clear 12:00 (the snow fades out) - from a camera with the tactical view's numbers (pitch -35.26, vertical FOV 30, 26 m; the far
	// still 40 m), and 10 s clips clip_<weather>/frame_NNNN.png at 15 fps (encode: ffmpeg -y -framerate 15 -i frame_%04d.png -vf
	// scale=960:-2 -c:v libx264 -pix_fmt yuv420p -crf 26 out.mp4). Weather forced instantly (a locked level stays locked), clock stopped.

	namespace Snow
	{
		constexpr int32 WarmupFrames = 150;
		constexpr int32 SettleFrames = 300;      // 10 s at -fps=30: the smoothed intensity rises and the flakes fill the view volume
		constexpr int32 ClipFrames = 300;        // 10 s, every 2nd frame -> 15 fps
		constexpr float ViewPitch = -35.26f;     // FTacticalCameraConfig defaults
		constexpr float ViewVerticalFov = 30.f;
		constexpr float ViewDistance = 2600.f;
		constexpr float FarDistance = 4000.f;

		struct FShot
		{
			EWeatherState Weather;
			float Hour;
			float Distance;
			bool bClip;
		};

		const FShot Shots[] = {
			{ EWeatherState::Snowfall, 12.f, ViewDistance, false }, { EWeatherState::Blizzard, 12.f, ViewDistance, false },
			{ EWeatherState::Blizzard, 12.f, FarDistance, false }, { EWeatherState::Snowfall, 23.f, ViewDistance, false },
			{ EWeatherState::Clear, 12.f, ViewDistance, false },
			{ EWeatherState::Snowfall, 12.f, ViewDistance, true }, { EWeatherState::Blizzard, 12.f, ViewDistance, true },
		};

		enum class EStep : uint8 { Warmup, Setup, Settle, Write, Clip, Done };

		struct FSnowFxRunner
		{
			EStep Step = EStep::Warmup;
			int32 Frame = 0;
			int32 StepFrame = 0;
			int32 ShotIndex = 0;
			int32 ClipCount = 0;
			float ViewYaw = 45.f;
			FString MapName;
			FString Dir;
			FDelegateHandle EndFrameHandle;
		};

		TSharedPtr<FSnowFxRunner> FxRunner;

		/** Shader jobs still compiling (a fresh material / Niagara system compiles on first use in an editor build). */
		int32 ShadersPending()
		{
#if WITH_EDITOR
			return GShaderCompilingManager ? GShaderCompilingManager->GetNumRemainingJobs() : 0;
#else
			return 0;
#endif
		}

		/** An own camera with the tactical camera's numbers on the squad leader (a level / tutorial camera cannot take the view). */
		void ViewSquad(UWorld* World, float Distance)
		{
			APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
			USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
			if (!PC || !Squad || !Squad->GetLeader())
			{
				return;
			}
			if (AHUD* Hud = PC->GetHUD())
			{
				Hud->bShowHUD = false;
			}
			const FVector Focus = Squad->GetLeader()->GetActorLocation();
			const FRotator Rotation(ViewPitch, FxRunner->ViewYaw, 0.f);
			const FVector Eye = Focus - Rotation.Vector() * Distance;
			if (!Runner->Camera.IsValid())
			{
				Runner->Camera = World->SpawnActor<ACameraActor>(Eye, Rotation);
			}
			if (ACameraActor* Camera = Runner->Camera.Get())
			{
				const FVector2D Size = UWidgetLayoutLibrary::GetViewportSize(World);
				const float Aspect = Size.Y > 1.f ? Size.X / Size.Y : 16.f / 9.f;
				const float HorizontalFov = FMath::RadiansToDegrees(2.f * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(ViewVerticalFov) * 0.5f) * Aspect));
				Camera->SetActorLocationAndRotation(Eye, Rotation);
				Camera->GetCameraComponent()->SetFieldOfView(HorizontalFov);
				Camera->GetCameraComponent()->bConstrainAspectRatio = false;
				if (PC->GetViewTarget() != Camera)
				{
					PC->SetViewTarget(Camera);
				}
			}
		}

		/** "active" / "off" of the presenter's falling-snow Niagara component. */
		FString SnowFxState(const AWeatherPresenterActor* Presenter)
		{
			const USceneComponent* SnowFx = Presenter ? Presenter->GetSnowFx() : nullptr;
			if (!SnowFx)
			{
				return TEXT("no snow fx component");
			}
			for (const USceneComponent* Child : SnowFx->GetAttachChildren())
			{
				if (Child)
				{
					return FString::Printf(TEXT("snow fx %s"), Child->IsActive() ? TEXT("active") : TEXT("off"));
				}
			}
			return TEXT("snow fx not spawned");
		}

		/** The falling-snow Niagara component is spawned and active (its system loaded and compiled). */
		bool IsSnowFxActive(const AWeatherPresenterActor* Presenter)
		{
			const USceneComponent* SnowFx = Presenter ? Presenter->GetSnowFx() : nullptr;
			for (const USceneComponent* Child : SnowFx ? SnowFx->GetAttachChildren() : TArray<TObjectPtr<USceneComponent>>())
			{
				if (Child && Child->IsActive())
				{
					return true;
				}
			}
			return false;
		}

		void Label(UEnvironmentSubsystem& Environment, const AWeatherPresenterActor* Presenter, const FShot& Shot)
		{
			FRunner& R = *Runner;
			R.Lines.Reset();
			R.Lines.Add({ FString::Printf(TEXT("%s - %s %s - camera %.0f m - %s"), *FxRunner->MapName, *EnvironmentRules::WeatherToString(Environment.GetWeather()),
				*HourText(Environment.GetHour()), Shot.Distance / 100.f, *SnowFxState(Presenter)), FLinearColor(1.f, 0.9f, 0.3f) });
		}

		void Finish()
		{
			UE_LOG(LogCodexTactics, Display, TEXT("EnvironmentShots snow: done (%d stills / clips, %d clip frames) -> %s"), FxRunner->ShotIndex,
				FxRunner->ClipCount, *FxRunner->Dir);
			FCoreDelegates::OnEndFrame.Remove(FxRunner->EndFrameHandle);
			UDebugDrawService::Unregister(Runner->DrawHandle);
			FPlatformMisc::RequestExit(false, TEXT("EnvironmentShots"));
		}

		FString ShotName(const FShot& Shot)
		{
			return FString::Printf(TEXT("%s_%s_%s%s"), *FxRunner->MapName, *EnvironmentRules::WeatherToString(Shot.Weather), *Visuals::HourTag(Shot.Hour),
				Shot.Distance > ViewDistance ? TEXT("_far") : TEXT(""));
		}

		void Tick()
		{
			FSnowFxRunner& S = *FxRunner;
			++S.Frame;
			++S.StepFrame;
			UWorld* World = FrontendSmokeUtils::FindGameWorld();
			if (S.Frame > MaxFrames)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("EnvironmentShots snow: timeout in step %d"), static_cast<int32>(S.Step));
				Finish();
				return;
			}
			UEnvironmentSubsystem* Environment = World ? UEnvironmentSubsystem::Get(World) : nullptr;
			USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr;
			if (!World || !Environment || !Environment->IsInitialised() || !Squad || !Squad->GetLeader())
			{
				return;
			}
			Runner->World = World;
			KeepSquadSafe(World);
			UWidgetLayoutLibrary::RemoveAllWidgets(World);
			const AWeatherPresenterActor* Presenter = AWeatherPresenterActor::Find(World);
			const FShot& Shot = Shots[FMath::Min(S.ShotIndex, static_cast<int32>(UE_ARRAY_COUNT(Shots)) - 1)];

			switch (S.Step)
			{
			case EStep::Warmup:
				if (S.StepFrame >= WarmupFrames && Presenter && Presenter->IsApplying() && ShadersPending() == 0)
				{
					S.MapName = World->GetMapName();
					S.MapName.RemoveFromStart(World->StreamingLevelsPrefix);
					if (S.MapName.Contains(TEXT("MovementTest")))
					{
						SmokeUtils::PlaceSquadAtTestStart(World);
					}
					if (const APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
					{
						S.ViewYaw = PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraRotation().Yaw : S.ViewYaw;
					}
					UE_LOG(LogCodexTactics, Display, TEXT("EnvironmentShots snow: %s, view yaw %.0f, %s"), *S.MapName, S.ViewYaw, *SnowFxState(Presenter));
					S.Step = EStep::Setup;
					S.StepFrame = 0;
				}
				break;

			case EStep::Setup:
				Environment->SetClockScale(0.f);
				Environment->ForceWeather(Shot.Weather, true);
				Environment->SetHour(Shot.Hour);
				ViewSquad(World, Shot.Distance);
				Label(*Environment, Presenter, Shot);
				S.Step = EStep::Settle;
				S.StepFrame = 0;
				break;

			case EStep::Settle:
				ViewSquad(World, Shot.Distance);
				Label(*Environment, Presenter, Shot);
				// The settle time counts once the snow runs (the first use loads and compiles NS_Weather_Snow in an editor build).
				if ((Shot.Weather == EWeatherState::Snowfall || Shot.Weather == EWeatherState::Blizzard) && !IsSnowFxActive(Presenter)
					&& S.StepFrame < SettleFrames * 4)
				{
					S.StepFrame = FMath::Min(S.StepFrame, 1);
				}
				if (S.StepFrame >= SettleFrames && ShadersPending() == 0)
				{
					if (Shot.bClip)
					{
						S.ClipCount = 0;
						S.Step = EStep::Clip;
					}
					else
					{
						Capture(S.Dir / (ShotName(Shot) + TEXT(".png")));
						UE_LOG(LogCodexTactics, Display, TEXT("EnvironmentShots snow: %s (%s, %s)"), *ShotName(Shot), *Environment->Describe(), *SnowFxState(Presenter));
						S.Step = EStep::Write;
					}
					S.StepFrame = 0;
				}
				break;

			case EStep::Clip:
				ViewSquad(World, Shot.Distance);
				Label(*Environment, Presenter, Shot);
				if (S.StepFrame % 2 == 0 && S.StepFrame <= ClipFrames)
				{
					const FString Clip = FString::Printf(TEXT("clip_%s"), *EnvironmentRules::WeatherToString(Shot.Weather).ToLower());
					Capture(S.Dir / Clip / FString::Printf(TEXT("frame_%04d.png"), S.ClipCount++));
				}
				if (S.StepFrame > ClipFrames + 3)
				{
					UE_LOG(LogCodexTactics, Display, TEXT("EnvironmentShots snow: clip %s, %d frames"), *EnvironmentRules::WeatherToString(Shot.Weather), S.ClipCount);
					S.Step = EStep::Write;
					S.StepFrame = 0;
				}
				break;

			case EStep::Write:
				if (S.StepFrame >= 10)
				{
					++S.ShotIndex;
					S.Step = S.ShotIndex < UE_ARRAY_COUNT(Shots) ? EStep::Setup : EStep::Done;
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

		void Run()
		{
			Runner = MakeShared<FRunner>();
			FxRunner = MakeShared<FSnowFxRunner>();
			FxRunner->Dir = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir() / TEXT("..") / TEXT("Environment") / TEXT("snow"));
			FPaths::CollapseRelativeDirectories(FxRunner->Dir);
			IFileManager::Get().DeleteDirectory(*(FxRunner->Dir / TEXT("clip_snowfall")), false, true);
			IFileManager::Get().DeleteDirectory(*(FxRunner->Dir / TEXT("clip_blizzard")), false, true);
			IFileManager::Get().MakeDirectory(*FxRunner->Dir, true);
			Runner->Dir = FxRunner->Dir;
			Runner->DrawHandle = UDebugDrawService::Register(TEXT("Game"), FDebugDrawDelegate::CreateStatic(&OnDraw));
			FxRunner->EndFrameHandle = FCoreDelegates::OnEndFrame.AddStatic(&Tick);
			UE_LOG(LogCodexTactics, Display, TEXT("EnvironmentShots snow: %d stills / clips -> %s"), static_cast<int32>(UE_ARRAY_COUNT(Shots)), *FxRunner->Dir);
		}
	}

	// --- Mode "snowperf": GPU / frame cost of the falling snow (NS_Weather_Snow) off vs Snowfall vs Blizzard ---

	namespace SnowPerf
	{
		constexpr int32 WarmupFrames = 150;
		constexpr int32 SettleFrames = 540;      // 9 s at -fps=60: the smoothed intensity rises and the flakes fill the view volume (7.5 s life)
		constexpr int32 MeasureFrames = 180;     // ~3 s at -fps=60
		constexpr int32 PerfMaxFrames = 24000;
		constexpr int32 WaveIndex = 2;

		struct FCase
		{
			const TCHAR* Name;
			EWeatherState Weather;
			bool bFx;
		};

		// "off" keeps the Blizzard weather (sky / fog / post) and only hides the Niagara system, so the A/B isolates the snow FX.
		const FCase Cases[] = {
			{ TEXT("snowfall"), EWeatherState::Snowfall, true }, { TEXT("blizzard"), EWeatherState::Blizzard, true }, { TEXT("off"), EWeatherState::Blizzard, false },
		};
		const float Distances[] = { Snow::ViewDistance, Snow::FarDistance };

		enum class EStep : uint8 { Warmup, Setup, Settle, Measure, Write, Done };

		struct FSample
		{
			double Gpu = 0.0;
			double Render = 0.0;
			double Game = 0.0;
			double Draws = 0.0;
			double Prims = 0.0;
			int32 Count = 0;
		};

		struct FPerfRunner
		{
			EStep Step = EStep::Warmup;
			int32 Frame = 0;
			int32 StepFrame = 0;
			int32 Index = 0; // distance-major: Index / Cases.Num = distance, Index % Cases.Num = case
			bool bWave = false;
			FString MapName;
			FString Dir;
			FSample Sample;
			TArray<FString> Report;
			FDelegateHandle EndFrameHandle;
		};

		TSharedPtr<FPerfRunner> PRunner;

		void Finish()
		{
			for (const FString& Line : PRunner->Report)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("[SnowFxPerf] %s"), *Line);
			}
			UE_LOG(LogCodexTactics, Display, TEXT("EnvironmentShots snowperf: done -> %s"), *PRunner->Dir);
			FCoreDelegates::OnEndFrame.Remove(PRunner->EndFrameHandle);
			UDebugDrawService::Unregister(Runner->DrawHandle);
			FPlatformMisc::RequestExit(false, TEXT("EnvironmentShots"));
		}

		void Tick()
		{
			FPerfRunner& S = *PRunner;
			++S.Frame;
			++S.StepFrame;
			UWorld* World = FrontendSmokeUtils::FindGameWorld();
			if (S.Frame > PerfMaxFrames)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("EnvironmentShots snowperf: timeout in step %d"), static_cast<int32>(S.Step));
				Finish();
				return;
			}
			UEnvironmentSubsystem* Environment = World ? UEnvironmentSubsystem::Get(World) : nullptr;
			USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr;
			if (!World || !Environment || !Environment->IsInitialised() || !Squad || !Squad->GetLeader())
			{
				return;
			}
			Runner->World = World;
			KeepSquadSafe(World);
			UWidgetLayoutLibrary::RemoveAllWidgets(World);
			AWeatherPresenterActor* Presenter = AWeatherPresenterActor::Find(World);
			UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
			const int32 CaseCount = static_cast<int32>(UE_ARRAY_COUNT(Cases));
			const FCase& Case = Cases[S.Index % CaseCount];
			const float Distance = Distances[FMath::Min(S.Index / CaseCount, static_cast<int32>(UE_ARRAY_COUNT(Distances)) - 1)];

			switch (S.Step)
			{
			case EStep::Warmup:
				if (S.StepFrame >= WarmupFrames && Presenter && Presenter->IsApplying() && Snow::ShadersPending() == 0)
				{
					S.MapName = World->GetMapName();
					S.MapName.RemoveFromStart(World->StreamingLevelsPrefix);
					if (S.MapName.Contains(TEXT("MovementTest")))
					{
						SmokeUtils::PlaceSquadAtTestStart(World);
					}
					if (const APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
					{
						Snow::FxRunner->ViewYaw = PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraRotation().Yaw : Snow::FxRunner->ViewYaw;
					}
					if (S.MapName.Contains(TEXT("MovementTest")) && Waves)
					{
						SmokeUtils::PlaceSquadAwayFromWaveSpawns(World, 3000.f);
						Waves->ClearAllEnemies();
						Waves->StartWave(WaveIndex);
						S.bWave = true;
						S.MapName += FString::Printf(TEXT("_wave%d"), WaveIndex);
						UE_LOG(LogCodexTactics, Display, TEXT("EnvironmentShots snowperf: wave %d started (%d alive)"), WaveIndex, Waves->GetAliveEnemyCount());
					}
					Environment->SetClockScale(0.f);
					Environment->SetHour(12.f);
					S.Step = EStep::Setup;
					S.StepFrame = 0;
				}
				break;

			case EStep::Setup:
				Environment->ForceWeather(Case.Weather, true);
				Environment->SetHour(12.f);
				if (UWeatherSnowFxComponent* SnowFx = Presenter ? Presenter->GetSnowFx() : nullptr)
				{
					SnowFx->SetVisualsEnabled(Case.bFx);
				}
				Snow::ViewSquad(World, Distance);
				S.Sample = FSample();
				S.Step = EStep::Settle;
				S.StepFrame = 0;
				break;

			case EStep::Settle:
				Snow::ViewSquad(World, Distance);
				// The settle time counts once the snow runs (the first use loads and compiles NS_Weather_Snow in an editor build).
				if (Case.bFx && !Snow::IsSnowFxActive(Presenter) && S.StepFrame < SettleFrames * 4)
				{
					S.StepFrame = FMath::Min(S.StepFrame, 1);
				}
				if (S.StepFrame >= SettleFrames && Snow::ShadersPending() == 0)
				{
					S.Step = EStep::Measure;
					S.StepFrame = 0;
				}
				break;

			case EStep::Measure:
			{
				Snow::ViewSquad(World, Distance);
				FSample& Sample = S.Sample;
				Sample.Gpu += FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0));
				Sample.Render += FPlatformTime::ToMilliseconds(GRenderThreadTime);
				Sample.Game += FPlatformTime::ToMilliseconds(GGameThreadTime);
				Sample.Draws += GNumDrawCallsRHI[0];
				Sample.Prims += GNumPrimitivesDrawnRHI[0];
				++Sample.Count;
				if (S.StepFrame >= MeasureFrames)
				{
					const double N = FMath::Max(Sample.Count, 1);
					const FString Tag = FString::Printf(TEXT("%s%s"), Case.Name, Distance > Snow::ViewDistance ? TEXT("_far") : TEXT(""));
					const FString Alive = S.bWave && Waves ? FString::Printf(TEXT(", %d enemies alive"), Waves->GetAliveEnemyCount()) : FString();
					S.Report.Add(FString::Printf(TEXT("%s | %s | camera %.0f m | GPU %.2f ms | render %.2f ms | game %.2f ms | draws %.0f | prims %.0f%s"), *S.MapName, Case.Name,
						Distance / 100.f, Sample.Gpu / N, Sample.Render / N, Sample.Game / N, Sample.Draws / N, Sample.Prims / N, *Alive));
					Capture(S.Dir / FString::Printf(TEXT("%s_%s.png"), *S.MapName, *Tag));
					S.Step = EStep::Write;
					S.StepFrame = 0;
				}
				break;
			}

			case EStep::Write:
				Snow::ViewSquad(World, Distance);
				if (S.StepFrame >= 10)
				{
					++S.Index;
					S.Step = S.Index < CaseCount * static_cast<int32>(UE_ARRAY_COUNT(Distances)) ? EStep::Setup : EStep::Done;
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

		void Run()
		{
			Runner = MakeShared<FRunner>();
			Snow::FxRunner = MakeShared<Snow::FSnowFxRunner>();
			PRunner = MakeShared<FPerfRunner>();
			PRunner->Dir = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir() / TEXT("..") / TEXT("Environment") / TEXT("snowperf"));
			FPaths::CollapseRelativeDirectories(PRunner->Dir);
			IFileManager::Get().MakeDirectory(*PRunner->Dir, true);
			Runner->Dir = PRunner->Dir;
			Runner->DrawHandle = UDebugDrawService::Register(TEXT("Game"), FDebugDrawDelegate::CreateStatic(&OnDraw));
			PRunner->EndFrameHandle = FCoreDelegates::OnEndFrame.AddStatic(&Tick);
			UE_LOG(LogCodexTactics, Display, TEXT("EnvironmentShots snowperf: %d cases x %d camera distances -> %s"), static_cast<int32>(UE_ARRAY_COUNT(Cases)),
				static_cast<int32>(UE_ARRAY_COUNT(Distances)), *PRunner->Dir);
		}
	}

	// --- Mode "snowcover": the gradual snow cover overlay (environment phase 4c) ---

	namespace SnowCover
	{
		constexpr int32 WarmupFrames = 150;
		constexpr int32 SettleFrames = 75;     // the overlay re-scan, TSR resolving the dithered edge, eye adaptation
		constexpr int32 MovieFrames = 300;     // 10 s at -fps=30: SnowAmount 0 -> 1
		constexpr int32 MovieEvery = 2;        // 15 fps video
		constexpr int32 PerfSettleFrames = 45;
		constexpr int32 PerfFrames = 90;
		constexpr float PerfSnowAmount = 0.7f;
		constexpr int32 MinWaveEnemies = 13;
		const float Amounts[] = { 0.f, 0.35f, 0.7f, 1.f };
		const TCHAR* MpcPath = TEXT("/Game/Environment/Weather/MPC/MPC_Environment.MPC_Environment");

		enum class EStep : uint8 { Warmup, Setup, Settle, Write, MovieSetup, Movie, PerfOff, PerfOn, WaveSetup, WaveOff, WaveOn, Done };

		struct FPerfSample
		{
			double Gpu = 0.0;
			double Render = 0.0;
			double Game = 0.0;
			double Draws = 0.0;
			double Prims = 0.0;
			int32 Count = 0;

			void Add()
			{
				Gpu += FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0));
				Render += FPlatformTime::ToMilliseconds(GRenderThreadTime);
				Game += FPlatformTime::ToMilliseconds(GGameThreadTime);
				Draws += GNumDrawCallsRHI[0];
				Prims += GNumPrimitivesDrawnRHI[0];
				++Count;
			}

			FString Describe() const
			{
				const double N = FMath::Max(Count, 1);
				return FString::Printf(TEXT("GPU %.2f ms, render %.2f ms, game %.2f ms, draws %.0f, prims %.0f"), Gpu / N, Render / N, Game / N, Draws / N, Prims / N);
			}
		};

		struct FSnowRunner
		{
			EStep Step = EStep::Warmup;
			int32 Frame = 0;
			int32 StepFrame = 0;
			int32 ShotIndex = 0;
			int32 ClipCount = 0;
			int32 WaveIndex = 0;
			bool bOwnOverlay = false;
			bool bMovie = true;
			bool bPerf = true;
			FString MapName;
			FString Dir;
			FVector Focus = FVector::ZeroVector;
			float Distance = 3000.f;
			FPerfSample Off;
			FPerfSample On;
			TArray<FString> Report;
			TWeakObjectPtr<UWeatherSnowOverlayComponent> Overlay;
			TWeakObjectPtr<UMaterialParameterCollection> Mpc;
			FDelegateHandle EndFrameHandle;
		};

		TSharedPtr<FSnowRunner> SRunner;

		/** Shader jobs still compiling (a fresh overlay material compiles on first use in an editor build). */
		int32 ShadersPending()
		{
#if WITH_EDITOR
			return GShaderCompilingManager ? GShaderCompilingManager->GetNumRemainingJobs() : 0;
#else
			return 0;
#endif
		}

		/** Static meshes holding the overlay material right now. */
		int32 CountOverlayMeshes(UWorld* World)
		{
			const UWeatherSnowOverlayComponent* Overlay = SRunner->Overlay.Get();
			const UMaterialInterface* Material = Overlay ? Overlay->OverlayMaterial.Get() : nullptr;
			int32 Count = 0;
			for (TObjectIterator<UStaticMeshComponent> It; It && Material; ++It)
			{
				if (It->GetWorld() == World && It->GetOverlayMaterial() == Material)
				{
					++Count;
				}
			}
			return Count;
		}

		/** The densest cluster of props / buildings in the level (objects with a top to whiten) is the centre of the wide shot. */
		void PickFocus(UWorld* World)
		{
			FSnowRunner& S = *SRunner;
			USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
			const FVector Leader = Squad && Squad->GetLeader() ? Squad->GetLeader()->GetActorLocation() : FVector::ZeroVector;
			TArray<FBox> Objects;
			for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
			{
				const UStaticMeshComponent* Mesh = *It;
				const AActor* Owner = Mesh->GetOwner();
				if (Mesh->GetWorld() != World || !Mesh->IsRegistered() || !Mesh->IsVisible() || !Owner || Owner->IsA<APawn>()
					|| Owner->GetClass()->GetName().Contains(TEXT("Landscape")) || Mesh->IsA<UInstancedStaticMeshComponent>())
				{
					continue;
				}
				const FBoxSphereBounds& Bounds = Mesh->Bounds;
				const float Footprint = 2.f * FMath::Max(Bounds.BoxExtent.X, Bounds.BoxExtent.Y);
				if (Footprint < 120.f || Footprint > 4000.f || Bounds.BoxExtent.Z < 40.f)
				{
					continue;
				}
				Objects.Add(Bounds.GetBox());
				UE_LOG(LogCodexTactics, Verbose, TEXT("EnvironmentShots snowcover: object %s.%s at %s"), *Owner->GetName(), *Mesh->GetName(), *Bounds.Origin.ToCompactString());
			}
			S.Focus = Leader;
			int32 BestCount = 0;
			for (const FBox& Candidate : Objects)
			{
				int32 Count = 0;
				for (const FBox& Other : Objects)
				{
					Count += FVector::Dist2D(Candidate.GetCenter(), Other.GetCenter()) < 1500.f ? 1 : 0;
				}
				// Ties: the cluster nearest to the squad.
				if (Count > BestCount || (Count == BestCount && FVector::Dist2D(Candidate.GetCenter(), Leader) < FVector::Dist2D(S.Focus, Leader)))
				{
					BestCount = Count;
					S.Focus = FVector(Candidate.GetCenter().X, Candidate.GetCenter().Y, Candidate.Min.Z);
				}
			}
			S.Distance = 3000.f;
			UE_LOG(LogCodexTactics, Display, TEXT("EnvironmentShots snowcover: focus %s (%d of %d objects within 15 m), distance %.0f"), *S.Focus.ToCompactString(),
				BestCount, Objects.Num(), S.Distance);
		}

		/** Wide shot over the focus from the tactical view's side (pitch -38). */
		void ViewFocus(UWorld* World)
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
			const FRotator Rotation(-38.f, Visuals::ViewYaw, 0.f);
			const FVector Eye = SRunner->Focus - Rotation.Vector() * SRunner->Distance;
			if (!Runner->Camera.IsValid())
			{
				Runner->Camera = World->SpawnActor<ACameraActor>(Eye, Rotation);
			}
			if (ACameraActor* Camera = Runner->Camera.Get())
			{
				Camera->SetActorLocationAndRotation(Eye, Rotation);
				Camera->GetCameraComponent()->SetFieldOfView(65.f);
				Camera->GetCameraComponent()->bConstrainAspectRatio = false;
				if (PC->GetViewTarget() != Camera)
				{
					PC->SetViewTarget(Camera);
				}
			}
		}

		/** The snow cover through the environment state (the presenter writes it to MPC_Environment); drives an own overlay component too. */
		void SetSnow(UWorld* World, UEnvironmentSubsystem& Environment, AWeatherPresenterActor* Presenter, float Amount)
		{
			FSnowRunner& S = *SRunner;
			if (!FMath::IsNearlyEqual(Environment.GetSnowAmount(), Amount, 0.0005f))
			{
				Environment.SetSnowAmount(Amount);
			}
			if (S.bOwnOverlay)
			{
				// The presenter does not own an overlay component (yet): write the MPC and drive ours like it would.
				const float Frost = Presenter ? Presenter->GetLastFrame().Preset.FrostAmount : 0.f;
				if (UMaterialParameterCollection* Mpc = S.Mpc.Get())
				{
					UKismetMaterialLibrary::SetScalarParameterValue(World, Mpc, TEXT("SnowAmount"), Environment.GetSnowAmount());
					UKismetMaterialLibrary::SetScalarParameterValue(World, Mpc, TEXT("FrostAmount"), Frost);
				}
				if (UWeatherSnowOverlayComponent* Overlay = S.Overlay.Get())
				{
					Overlay->ApplySnowCover(Environment.GetSnowAmount(), Frost);
				}
			}
		}

		void Label(UWorld* World, UEnvironmentSubsystem& Environment)
		{
			Runner->Lines.Reset();
			Runner->Lines.Add({ FString::Printf(TEXT("%s - %s %s - snow cover %.0f%% - overlay on %d meshes"), *SRunner->MapName,
				*EnvironmentRules::WeatherToString(Environment.GetWeather()), *HourText(Environment.GetHour()), Environment.GetSnowAmount() * 100.f,
				CountOverlayMeshes(World)), FLinearColor(1.f, 0.9f, 0.3f) });
		}

		/** The level's largest wave as written (1-based) when it has at least MinWaveEnemies enemies, else 0. */
		int32 FindBigWave(const UWorld* World, int32& OutCount)
		{
			OutCount = 0;
			const ACodexTacticsGameMode* GameMode = World ? World->GetAuthGameMode<ACodexTacticsGameMode>() : nullptr;
			const ULevelConfigAsset* Level = GameMode ? GameMode->GetActiveLevelConfig() : nullptr;
			if (!Level)
			{
				return 0;
			}
			int32 Best = 0;
			for (int32 Index = 0; Index < Level->Config.Waves.Num(); ++Index)
			{
				int32 Count = 0;
				for (const FEnemySpawnEntry& Entry : Level->Config.Waves[Index].Spawns)
				{
					Count += Entry.Count;
				}
				if (Count > OutCount)
				{
					OutCount = Count;
					Best = Index + 1;
				}
			}
			return OutCount >= MinWaveEnemies ? Best : 0;
		}

		void SetOverlayCVar(bool bOn)
		{
			if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Env.SnowOverlay")))
			{
				Variable->Set(bOn ? 1 : 0, ECVF_SetByConsole);
			}
		}

		void Finish()
		{
			FSnowRunner& S = *SRunner;
			for (const FString& Line : S.Report)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("[SnowPerf] %s"), *Line);
			}
			SetOverlayCVar(true);
			UE_LOG(LogCodexTactics, Display, TEXT("EnvironmentShots snowcover: done (%d stills, %d movie frames) -> %s"), S.ShotIndex, S.ClipCount, *S.Dir);
			FCoreDelegates::OnEndFrame.Remove(S.EndFrameHandle);
			UDebugDrawService::Unregister(Runner->DrawHandle);
			FPlatformMisc::RequestExit(false, TEXT("EnvironmentShots"));
		}

		void Tick()
		{
			FSnowRunner& S = *SRunner;
			++S.Frame;
			++S.StepFrame;
			UWorld* World = FrontendSmokeUtils::FindGameWorld();
			if (S.Frame > MaxFrames)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("EnvironmentShots snowcover: timeout in step %d"), static_cast<int32>(S.Step));
				Finish();
				return;
			}
			UEnvironmentSubsystem* Environment = World ? UEnvironmentSubsystem::Get(World) : nullptr;
			USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr;
			if (!World || !Environment || !Environment->IsInitialised() || !Squad || !Squad->GetLeader())
			{
				return;
			}
			Runner->World = World;
			KeepSquadSafe(World);
			UWidgetLayoutLibrary::RemoveAllWidgets(World);
			AWeatherPresenterActor* Presenter = AWeatherPresenterActor::Find(World);
			UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();

			switch (S.Step)
			{
			case EStep::Warmup:
				if (S.StepFrame >= WarmupFrames && Presenter && Presenter->IsApplying())
				{
					S.MapName = World->GetMapName();
					S.MapName.RemoveFromStart(World->StreamingLevelsPrefix);
					if (S.MapName.Contains(TEXT("MovementTest")))
					{
						SmokeUtils::PlaceSquadAtTestStart(World);
					}
					if (const APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
					{
						Visuals::ViewYaw = PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraRotation().Yaw : Visuals::ViewYaw;
					}
					S.Overlay = Presenter->FindComponentByClass<UWeatherSnowOverlayComponent>();
					if (!S.Overlay.IsValid())
					{
						UWeatherSnowOverlayComponent* Own = NewObject<UWeatherSnowOverlayComponent>(Presenter, TEXT("ShotsSnowOverlay"));
						Own->RegisterComponent();
						S.Overlay = Own;
						S.bOwnOverlay = true;
						S.Mpc = LoadObject<UMaterialParameterCollection>(nullptr, MpcPath);
					}
					UE_LOG(LogCodexTactics, Display, TEXT("EnvironmentShots snowcover: %s, overlay component %s, MPC %s"), *S.MapName,
						S.bOwnOverlay ? TEXT("own (the presenter has none)") : TEXT("the presenter's"), (S.Mpc.IsValid() || !S.bOwnOverlay) ? TEXT("ok") : TEXT("MISSING"));
					Environment->SetClockScale(0.f);
					Environment->ForceWeather(EWeatherState::Clear, true);
					Environment->SetHour(12.f);
					PickFocus(World);
					S.Step = EStep::Setup;
					S.StepFrame = 0;
				}
				break;

			case EStep::Setup:
				ViewFocus(World);
				SetSnow(World, *Environment, Presenter, Amounts[S.ShotIndex]);
				Label(World, *Environment);
				S.Step = EStep::Settle;
				S.StepFrame = 0;
				break;

			case EStep::Settle:
				ViewFocus(World);
				SetSnow(World, *Environment, Presenter, Amounts[S.ShotIndex]);
				Label(World, *Environment);
				// The first still also waits for the overlay's shaders / PSOs (compiled on first use in an editor build).
				if (S.StepFrame >= SettleFrames * (S.ShotIndex == 0 ? 4 : 1) && (ShadersPending() == 0 || S.StepFrame >= SettleFrames * 12))
				{
					const FString File = FString::Printf(TEXT("%s_snow_%03d.png"), *S.MapName, FMath::RoundToInt(Amounts[S.ShotIndex] * 100.f));
					Capture(S.Dir / File);
					UE_LOG(LogCodexTactics, Display, TEXT("EnvironmentShots snowcover: %s (%d overlay meshes, %s)"), *File, CountOverlayMeshes(World),
						*Environment->Describe());
					S.Step = EStep::Write;
					S.StepFrame = 0;
				}
				break;

			case EStep::Write:
				ViewFocus(World);
				SetSnow(World, *Environment, Presenter, Amounts[S.ShotIndex]);
				if (S.StepFrame >= 10)
				{
					++S.ShotIndex;
					S.Step = S.ShotIndex < UE_ARRAY_COUNT(Amounts) ? EStep::Setup : (S.bMovie ? EStep::MovieSetup : (S.bPerf ? EStep::PerfOff : EStep::Done));
					S.StepFrame = 0;
				}
				break;

			case EStep::MovieSetup:
				ViewFocus(World);
				SetSnow(World, *Environment, Presenter, 0.f);
				Label(World, *Environment);
				if (S.StepFrame >= 30)
				{
					S.Step = EStep::Movie;
					S.StepFrame = 0;
				}
				break;

			case EStep::Movie:
				ViewFocus(World);
				SetSnow(World, *Environment, Presenter, FMath::Clamp(static_cast<float>(S.StepFrame) / MovieFrames, 0.f, 1.f));
				Label(World, *Environment);
				if (S.StepFrame % MovieEvery == 0 && S.StepFrame <= MovieFrames)
				{
					Capture(S.Dir / (TEXT("movie_") + S.MapName) / FString::Printf(TEXT("frame_%04d.png"), S.ClipCount++));
				}
				if (S.StepFrame > MovieFrames + 3)
				{
					S.Step = S.bPerf ? EStep::PerfOff : EStep::Done;
					S.StepFrame = 0;
				}
				break;

			// Perf A/B at SnowAmount 0.7: overlay off (Codex.Env.SnowOverlay 0) vs on, on the still view and in a big wave fight.
			case EStep::PerfOff:
			case EStep::PerfOn:
			case EStep::WaveOff:
			case EStep::WaveOn:
			{
				const bool bOn = S.Step == EStep::PerfOn || S.Step == EStep::WaveOn;
				const bool bWave = S.Step == EStep::WaveOff || S.Step == EStep::WaveOn;
				if (S.StepFrame == 1)
				{
					SetOverlayCVar(bOn);
					Runner->Lines.Reset();
				}
				if (bWave)
				{
					FrameEnemies(World);
				}
				else
				{
					ViewFocus(World);
				}
				SetSnow(World, *Environment, Presenter, PerfSnowAmount);
				FPerfSample& Sample = bOn ? S.On : S.Off;
				if (S.StepFrame > PerfSettleFrames)
				{
					Sample.Add();
				}
				if (S.StepFrame >= PerfSettleFrames + PerfFrames)
				{
					if (bOn)
					{
						const FString Scene = bWave ? FString::Printf(TEXT("wave %d fight (%d enemies alive)"), S.WaveIndex, Waves ? Waves->GetAliveEnemyCount() : 0)
							: FString(TEXT("still view"));
						S.Report.Add(FString::Printf(TEXT("%s %s, snow %.1f: overlay OFF %s | ON (%d meshes) %s"), *S.MapName, *Scene, PerfSnowAmount,
							*S.Off.Describe(), CountOverlayMeshes(World), *S.On.Describe()));
						S.Off = FPerfSample();
						S.On = FPerfSample();
					}
					S.Step = S.Step == EStep::PerfOff ? EStep::PerfOn
						: (S.Step == EStep::PerfOn ? EStep::WaveSetup : (S.Step == EStep::WaveOff ? EStep::WaveOn : EStep::Done));
					S.StepFrame = 0;
				}
				break;
			}

			case EStep::WaveSetup:
			{
				int32 Count = 0;
				S.WaveIndex = FindBigWave(World, Count);
				if (!S.WaveIndex || !Waves)
				{
					S.Report.Add(FString::Printf(TEXT("%s: no wave with %d+ enemies (largest %d) - no wave measurement here"), *S.MapName, MinWaveEnemies, Count));
					S.Step = EStep::Done;
					S.StepFrame = 0;
					break;
				}
				if (S.StepFrame == 1)
				{
					SmokeUtils::PlaceSquadAwayFromWaveSpawns(World, 3000.f);
					Waves->ClearAllEnemies();
					Waves->StartWave(S.WaveIndex);
					UE_LOG(LogCodexTactics, Display, TEXT("EnvironmentShots snowcover: wave %d started (%d written, %d alive)"), S.WaveIndex, Count,
						Waves->GetAliveEnemyCount());
				}
				FrameEnemies(World);
				SetSnow(World, *Environment, Presenter, PerfSnowAmount);
				if (S.StepFrame >= 90)
				{
					Capture(S.Dir / FString::Printf(TEXT("%s_wave%d_snow_070.png"), *S.MapName, S.WaveIndex));
					S.Step = EStep::WaveOff;
					S.StepFrame = 0;
				}
				break;
			}

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
			auto Has = [&Args](const TCHAR* Word)
			{
				return Args.ContainsByPredicate([Word](const FString& Arg) { return Arg.Equals(Word, ESearchCase::IgnoreCase); });
			};
			Runner = MakeShared<FRunner>();
			SRunner = MakeShared<FSnowRunner>();
			SRunner->bMovie = !Has(TEXT("nomovie"));
			SRunner->bPerf = !Has(TEXT("noperf"));
			SRunner->Dir = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir() / TEXT("..") / TEXT("Environment") / TEXT("snowcover"));
			FPaths::CollapseRelativeDirectories(SRunner->Dir);
			IFileManager::Get().MakeDirectory(*SRunner->Dir, true);
			Runner->Dir = SRunner->Dir;
			Runner->DrawHandle = UDebugDrawService::Register(TEXT("Game"), FDebugDrawDelegate::CreateStatic(&OnDraw));
			SRunner->EndFrameHandle = FCoreDelegates::OnEndFrame.AddStatic(&Tick);
			UE_LOG(LogCodexTactics, Display, TEXT("EnvironmentShots snowcover: %d stills%s%s -> %s"), static_cast<int32>(UE_ARRAY_COUNT(Amounts)),
				SRunner->bMovie ? TEXT(" + movie") : TEXT(""), SRunner->bPerf ? TEXT(" + perf A/B") : TEXT(""), *SRunner->Dir);
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() > 0 && Args[0].Equals(TEXT("snowcover"), ESearchCase::IgnoreCase))
		{
			SnowCover::Run(Args);
			return;
		}
		if (Args.Num() > 0 && Args[0].Equals(TEXT("snowperf"), ESearchCase::IgnoreCase))
		{
			SnowPerf::Run();
			return;
		}
		if (Args.Num() > 0 && Args[0].Equals(TEXT("snow"), ESearchCase::IgnoreCase))
		{
			Snow::Run();
			return;
		}
		if (Args.Num() > 0 && Args[0].Equals(TEXT("visuals"), ESearchCase::IgnoreCase))
		{
			Visuals::Run(Args.ContainsByPredicate([](const FString& Arg) { return Arg.Equals(TEXT("timelapse"), ESearchCase::IgnoreCase); }));
			return;
		}
		Runner = MakeShared<FRunner>();
		Runner->Dir = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir() / TEXT("..") / TEXT("Environment"));
		FPaths::CollapseRelativeDirectories(Runner->Dir);
		IFileManager::Get().DeleteDirectory(*(Runner->Dir / TEXT("timelapse")), false, true);
		IFileManager::Get().MakeDirectory(*(Runner->Dir / TEXT("timelapse")), true);
		Runner->DrawHandle = UDebugDrawService::Register(TEXT("Game"), FDebugDrawDelegate::CreateStatic(&OnDraw));
		Runner->EndFrameHandle = FCoreDelegates::OnEndFrame.AddStatic(&Tick);
		UE_LOG(LogCodexTactics, Display, TEXT("EnvironmentShots: timelapse x%.0f + 2 stills -> %s"), TimelapseClockScale, *Runner->Dir);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.EnvironmentShots"),
		TEXT("Dev: rendered environment captures into Saved/Screenshots/Environment (timelapse frames + wave stills under Clear midday / Blizzard night); 'visuals [timelapse]': the weather presenter stills (+ a day at x60) into Environment/visuals; 'snowcover [nomovie] [noperf]': the snow cover overlay stills, movie and perf A/B into Environment/snowcover; 'snowperf': the falling snow GPU cost off / Snowfall / Blizzard at 2 camera distances into Environment/snowperf; 'snow': the falling snow stills + 10 s clips into Environment/snow; needs rendering, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
