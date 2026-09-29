// Dev-only console command for a headless spawn point filter / dynamic flank breach check on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.FlankBreachSmoke
// Spitters never use the hound-only point and hounds never the spitter-only one; a JSON lane that names no point
// (Godot's "WEST_FLANK" vs «Левый фланг (Прорыв)») falls back to any static point; a dynamic point is never a wave
// point. With the breach on wave 1 and instant events, starting the fight spawns the pack around the dynamic point
// with the HQ line; 1 s later the camera shows the point with the squad's line, 1.8 s later it is back on the leader
// (Godot enemy_spawn_point.gd trigger_breach, main.gd _get_enemy_spawn_pos / _check_dynamic_flank_spawners).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/TacticalCameraPawn.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/EnemySpawnPoint.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "UI/GameMessageSubsystem.h"

namespace FlankBreachSmoke
{
	constexpr float StepSeconds = 0.25f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		int32 EnemiesBefore = 0;
		TWeakObjectPtr<AEnemySpawnPoint> Breach;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("FlankBreachSmoke"));
		return false;
	}

	bool IsAt(const FVector& Location, const AEnemySpawnPoint* Point)
	{
		return Point && FVector::Dist2D(Location, Point->GetActorLocation()) < 1.f;
	}

	bool HasMessage(UWorld* World, const FString& Speaker, const FString& Part)
	{
		for (const FGameMessage& Message : World->GetSubsystem<UGameMessageSubsystem>()->GetHistory())
		{
			if (Message.Speaker.ToString() == Speaker && Message.Text.ToString().Contains(Part))
			{
				return true;
			}
		}
		return false;
	}

	int32 CountEnemiesNear(UWorld* World, const FVector& Center, EEnemyArchetype Type)
	{
		int32 Count = 0;
		for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
		{
			Count += It->GetArchetype() == Type && FVector::Dist2D(It->GetActorLocation(), Center) < 400.f ? 1 : 0;
		}
		return Count;
	}

	void CheckFilters(UWorld* World, FState& State)
	{
		UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
		const AEnemySpawnPoint* HoundOnly = nullptr;
		const AEnemySpawnPoint* SpitterOnly = nullptr;
		int32 StaticPoints = 0;
		for (TActorIterator<AEnemySpawnPoint> It(World); It; ++It)
		{
			if (It->bIsDynamic)
			{
				continue;
			}
			++StaticPoints;
			HoundOnly = It->AllowedEnemyType == EEnemySpawnFilter::Hound ? *It : HoundOnly;
			SpitterOnly = It->AllowedEnemyType == EEnemySpawnFilter::Spitter ? *It : SpitterOnly;
		}
		Check(State, HoundOnly && SpitterOnly && StaticPoints == 4, FString::Printf(TEXT("level: 4 points, hound-only and spitter-only (%d)"), StaticPoints));

		bool bSpitterOk = true, bHoundOk = true, bBruteOk = true, bDynamicSkipped = true;
		TSet<FVector> LaneFallback;
		for (int32 Index = 0; Index < 60; ++Index)
		{
			const FVector Spitter = Waves->GetSpawnLocationForLane(TEXT("ANY"), EEnemyArchetype::Spitter);
			const FVector Hound = Waves->GetSpawnLocationForLane(TEXT("ANY"), EEnemyArchetype::FrostHound);
			const FVector Brute = Waves->GetSpawnLocationForLane(TEXT("ANY"), EEnemyArchetype::Brute);
			const FVector Lane = Waves->GetSpawnLocationForLane(TEXT("WEST_FLANK"), EEnemyArchetype::Brute);
			bSpitterOk &= !IsAt(Spitter, HoundOnly);
			bHoundOk &= !IsAt(Hound, SpitterOnly);
			bBruteOk &= !IsAt(Brute, HoundOnly) && !IsAt(Brute, SpitterOnly);
			for (const FVector& Point : { Spitter, Hound, Brute, Lane })
			{
				bDynamicSkipped &= !IsAt(Point, State.Breach.Get());
			}
			LaneFallback.Add(Lane);
		}
		Check(State, bSpitterOk, TEXT("spitters never at the hound-only point"));
		Check(State, bHoundOk, TEXT("hounds never at the spitter-only point"));
		Check(State, bBruteOk, TEXT("brutes only at the open points"));
		Check(State, bDynamicSkipped, TEXT("the dynamic point is never a wave point"));
		Check(State, LaneFallback.Num() >= 3, FString::Printf(TEXT("unmatched lane: any static point (%d distinct)"), LaneFallback.Num()));
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time < 3.f)
		{
			return World != nullptr;
		}
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
		AOperativeCharacter* Leader = World->GetSubsystem<USquadSubsystem>()->GetLeader();
		ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(UGameplayStatics::GetPlayerPawn(World, 0));
		if (!Flow || !Waves || !Leader || !Camera || State.Time > 30.f)
		{
			Check(State, false, TEXT("flow, waves, leader, camera / timeout"));
			return Finish(State, false);
		}
		auto Next = [&State]() { ++State.Stage; State.StageTime = 0.f; };
		switch (State.Stage)
		{
		case 0:
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AEnemySpawnPoint* Point = World->SpawnActor<AEnemySpawnPoint>(SmokeUtils::LevelPoint(World, FVector(-700.f, -2300.f, 100.f)),
				FRotator::ZeroRotator, Params);
			Point->bIsDynamic = true;
			Point->ActivationChance = 1.f;
			Point->EnemyCount = 3;
			Point->BreachEnemyType = EEnemyArchetype::Spitter;
			Point->SpawnLane = TEXT("Тестовый фланг");
			State.Breach = Point;
			CheckFilters(World, State);

			Waves->SetRandomEventWaves(1, 2);
			Waves->bInstantRandomEvents = true;
			State.EnemiesBefore = Waves->GetAliveEnemyCount();
			Flow->TriggerCombatZone();
			Flow->FinishCutscene();
			Flow->FinishPreparation();
			Next();
			return true;
		}
		case 1:
		{
			const FVector Center = State.Breach->GetActorLocation();
			const int32 Pack = CountEnemiesNear(World, Center, EEnemyArchetype::Spitter);
			Check(State, Waves->IsDynamicBreachTriggered() && Pack == 3, FString::Printf(TEXT("breach: %d spitters around the point"), Pack));
			Check(State, HasMessage(World, TEXT("ШТАБ"), TEXT("ПРОРЫВ ВО ФЛАНГЕ! Враги пробили переборку на рубеже «Тестовый фланг»")), TEXT("HQ breach line"));
			Check(State, Camera->GetFollowTarget() == Leader, TEXT("camera still on the leader at once"));
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->CustomTimeDilation = 0.f; // keep the squad alive while the camera is checked
			}
			Next();
			return true;
		}
		case 2:
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			Check(State, Camera->GetFollowTarget() == State.Breach.Get(), TEXT("1 s later the camera shows the breach"));
			Check(State, HasMessage(World, TEXT("ОТРЯД"), TEXT("откуда они взялись")), TEXT("squad line"));
			Next();
			return true;
		case 3:
			if (State.StageTime < 2.f)
			{
				return true;
			}
			Check(State, Camera->GetFollowTarget() == Leader, TEXT("1.8 s later the camera is back on the leader"));
			Waves->CheckDynamicFlankSpawners(1);
			Check(State, CountEnemiesNear(World, State.Breach->GetActorLocation(), EEnemyArchetype::Spitter) == 3, TEXT("one breach per mission"));
			return Finish(State, true);
		default:
			return Finish(State, false);
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		{
			FTimerHandle PlaceHandle;
			TWeakObjectPtr<UWorld> PlaceWorld(World);
			World->GetTimerManager().SetTimer(PlaceHandle, FTimerDelegate::CreateLambda([PlaceWorld]()
			{
				SmokeUtils::PlaceSquadAtTestStart(PlaceWorld.Get());
			}), 0.5f, false);
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.FlankBreachSmoke"),
		TEXT("Dev check: spawn point type filters, dynamic flank breach pack, camera focus and return; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
