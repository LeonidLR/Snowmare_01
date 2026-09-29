// Dev-only console command for a headless click-rule check on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.ClickRulesSmoke
// Godot main.gd plain-click rules in a fight: an enemy becomes the priority target; a barrel / set-up item can't be
// moved outside the tactical pause (HQ line), a ground click can't move the squad either; in the pause a barrel is
// picked up for relocation at once and a barricade opens its menu at once.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/BarrelActor.h"
#include "Interactables/BarricadeActor.h"
#include "Interactables/InteractionSubsystem.h"
#include "Interactables/RelocationSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "UI/GameMessageSubsystem.h"

namespace ClickRulesSmoke
{
	constexpr float StepSeconds = 0.25f;

	struct FState
	{
		float Time = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("ClickRulesSmoke"));
		return false;
	}

	int32 CountMessages(UWorld* World, const FString& Part)
	{
		int32 Count = 0;
		for (const FGameMessage& Message : World->GetSubsystem<UGameMessageSubsystem>()->GetHistory())
		{
			Count += Message.Text.ToString().Contains(Part) ? 1 : 0;
		}
		return Count;
	}

	FHitResult HitOn(AActor* Actor, const FVector& Point)
	{
		return FHitResult(Actor, nullptr, Point, FVector::UpVector);
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time < 3.f)
		{
			return World != nullptr;
		}
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		AOperativeCharacter* Leader = World->GetSubsystem<USquadSubsystem>()->GetLeader();
		if (!Flow || !PC || !Leader)
		{
			Check(State, false, TEXT("flow, controller, leader"));
			return Finish(State, false);
		}
		for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
		{
			It->CustomTimeDilation = 0.f;
		}
		if (State.Stage++ > 0)
		{
			return Finish(State, false);
		}
		Flow->TriggerCombatZone();
		Flow->FinishCutscene();
		Flow->FinishPreparation();
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const FVector Front = Leader->GetActorLocation() + Leader->GetActorForwardVector() * 400.f;
		AEnemyCharacter* Hound = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::FrostHound, Front + FVector(0.f, 300.f, 0.f));
		ABarrelActor* Barrel = World->SpawnActor<ABarrelActor>(Front - FVector(0.f, 300.f, 20.f), FRotator::ZeroRotator, Params);
		ABarricadeActor* Barricade = World->SpawnActor<ABarricadeActor>(Front + FVector(300.f, 0.f, -40.f), FRotator::ZeroRotator, Params);
		if (!Hound || !Barrel || !Barricade)
		{
			Check(State, false, TEXT("hound, barrel, barricade spawned"));
			return Finish(State, false);
		}
		Hound->CustomTimeDilation = 0.f;

		// Real-time fight.
		PC->HandleWorldHit(HitOn(Hound, Hound->GetActorLocation()));
		Check(State, Leader->GetManualPriorityTarget() == Hound && CountMessages(World, TEXT("Назначена приоритетная цель")) > 0,
			TEXT("plain click on an enemy: priority target"));
		const int32 Refusals = CountMessages(World, TEXT("менять расположение объектов нельзя"));
		PC->HandleWorldHit(HitOn(Barrel, Barrel->GetActorLocation()));
		PC->HandleWorldHit(HitOn(Barricade, Barricade->GetActorLocation()));
		URelocationSubsystem* Relocation = World->GetSubsystem<URelocationSubsystem>();
		Check(State, CountMessages(World, TEXT("менять расположение объектов нельзя")) == Refusals + 2 && !Relocation->IsPlacing(),
			TEXT("barrel / barricade in the fight: HQ refusal"));
		const int32 MoveRefusals = CountMessages(World, TEXT("Перемещение во время боя возможно только"));
		PC->HandleWorldHit(HitOn(nullptr, Leader->GetActorLocation() + FVector(300.f, 0.f, -90.f)));
		Check(State, CountMessages(World, TEXT("Перемещение во время боя возможно только")) == MoveRefusals + 1, TEXT("ground click in the fight: HQ refusal"));

		// Tactical pause.
		Flow->ToggleTacticalPause();
		PC->HandleWorldHit(HitOn(Barrel, Barrel->GetActorLocation()));
		Check(State, Relocation->IsPlacing(), TEXT("pause: barrel picked up for relocation at once"));
		Relocation->CancelPlacement();
		PC->HandleWorldHit(HitOn(Barricade, Barricade->GetActorLocation()));
		Check(State, World->GetSubsystem<UInteractionSubsystem>()->IsActionMenuOpen(), TEXT("pause: barricade menu opens at once"));
		return Finish(State, true);
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
		TEXT("CodexTactics.ClickRulesSmoke"),
		TEXT("Dev check: plain-click rules in a fight and in the tactical pause; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
