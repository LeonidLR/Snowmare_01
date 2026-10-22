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

	void Run(const TArray<FString>& Args, UWorld* World)
	{
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
		TEXT("Dev: rendered environment captures into Saved/Screenshots/Environment (timelapse frames + wave stills under Clear midday / Blizzard night); needs rendering, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
