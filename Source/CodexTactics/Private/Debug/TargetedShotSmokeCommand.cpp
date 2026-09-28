// Dev-only console command for a headless Ctrl + click targeted-shot check on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.TargetedShotSmoke
// Drives ACodexTacticsPlayerController::IssueTargetedShot (the Ctrl + click path) for the commander:
// 1. barrel explodes (tracer + target flash); 2. mine shot while prone detonates it; 3. trapped crate is blown up,
// an untrapped one is only pierced; 4. trapped barricade is detonated (each shot costs one round); 5. an enemy becomes the priority target over a nearer one; 6. during a
// tactical pause a barrel shot is only planned (with a marker), and fires when the pause is released.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Combat/CombatFeedbackActor.h"
#include "Combat/CombatFeedbackSubsystem.h"
#include "EngineUtils.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/BarricadeActor.h"
#include "Interactables/BarrelActor.h"
#include "Interactables/LootCrateActor.h"
#include "Interactables/ProximityMineActor.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

namespace TargetedShotSmoke
{
	constexpr float NavWarmupSeconds = 3.f;
	constexpr float StepSeconds = 0.25f;
	constexpr float Timeout = 40.f;
	constexpr float TargetDistance = 900.f;

	struct FState
	{
		int32 Stage = 0;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 ClipBefore = 0;
		TWeakObjectPtr<ABarrelActor> Barrel;
		TWeakObjectPtr<AProximityMineActor> Mine;
		TWeakObjectPtr<ALootCrateActor> Crate;
		TWeakObjectPtr<ALootCrateActor> PlainCrate;
		TWeakObjectPtr<ABarricadeActor> Barricade;
		TWeakObjectPtr<AEnemyCharacter> NearEnemy;
		TWeakObjectPtr<AEnemyCharacter> FarEnemy;
		TWeakObjectPtr<ABarrelActor> PauseBarrel;
		int32 Failures = 0;
	};

	void Check(FState& State, bool bOk, const TCHAR* What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), What);
		State.Failures += bOk ? 0 : 1;
	}

	void Finish(const FState& State)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && State.Stage >= 9 ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("TargetedShotSmoke"));
	}

	FVector Feet(const AOperativeCharacter* Operative)
	{
		return Operative->GetActorLocation() - FVector(0.f, 0.f, Operative->GetSimpleCollisionHalfHeight());
	}

	template <typename T>
	T* SpawnAt(UWorld* World, const AOperativeCharacter* Commander, const FVector& Direction, float Distance, float Z)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const FVector Spot = Feet(Commander) + Direction.GetSafeNormal2D() * Distance + FVector(0.f, 0.f, Z);
		return World->SpawnActor<T>(Spot, FRotator::ZeroRotator, Params);
	}

	int32 CountFeedback(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<ACombatFeedbackActor> It(World); It; ++It)
		{
			++Count;
		}
		return Count;
	}

	void Next(FState& State)
	{
		++State.Stage;
		State.StageTime = 0.f;
	}

	/** Returns false when finished. Runs on the core ticker: real time, unaffected by the pause time dilation. */
	bool Step(UWorld* World, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		AOperativeCharacter* Commander = Squad && Squad->GetMembers().Num() > 0 ? Squad->GetMembers()[0] : nullptr;
		if (!Commander || !PC || !Flow || State.Time > Timeout)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke stopped in stage %d at %.1fs"), State.Stage, State.Time);
			Check(State, false, TEXT("finished in time"));
			Finish(State);
			return false;
		}
		const FVector Forward = Commander->GetActorForwardVector();
		const FVector Right = Commander->GetActorRightVector();

		switch (State.Stage)
		{
		case 0: // Barrel: explodes at once.
			Squad->SetLeader(Commander);
			State.ClipBefore = Commander->CurrentClip;
			PC->IssueTargetedShot(State.Barrel.Get());
			Check(State, State.Barrel.IsValid() && State.Barrel->IsBurning(), TEXT("barrel exploded"));
			Check(State, Commander->CurrentClip == State.ClipBefore - 1, TEXT("barrel shot used one round"));
			Check(State, CountFeedback(World) >= 2, TEXT("tracer and target flash spawned"));
			Commander->SetStance(EOperativeStance::Prone);
			Next(State);
			break;
		case 1: // Mine: prone at 9 m -> 95 %; forced hit keeps the check deterministic.
			Commander->bForceHitForTesting = true;
			PC->IssueTargetedShot(State.Mine.Get());
			Commander->bForceHitForTesting = false;
			Check(State, !IsValid(State.Mine.Get()), TEXT("mine detonated by the shot"));
			Next(State);
			break;
		case 2: // Trapped crate: remote detonation destroys it.
			PC->IssueTargetedShot(State.Crate.Get());
			Check(State, State.Crate.IsValid() && State.Crate->IsDestroyed() && !State.Crate->bTrapped, TEXT("trapped crate blown up"));
			PC->IssueTargetedShot(State.PlainCrate.Get());
			Check(State, State.PlainCrate.IsValid() && !State.PlainCrate->IsDestroyed(), TEXT("untrapped crate only pierced"));
			Next(State);
			break;
		case 3: // Trapped barricade: trap detonated, barricade stays.
			PC->IssueTargetedShot(State.Barricade.Get());
			Check(State, State.Barricade.IsValid() && !State.Barricade->bTrapped, TEXT("barricade trap detonated"));
			Check(State, Commander->CurrentClip == State.ClipBefore - 5, TEXT("five object shots used five rounds"));
			PC->IssueTargetedShot(nullptr); // ground: hint only
			Commander->SetStance(EOperativeStance::Standing);
			Next(State);
			break;
		case 4: // Priority target: the far enemy wins over the nearer one.
			if (State.StageTime >= 0.5f)
			{
				Check(State, Commander->FindBestCombatTarget() == State.NearEnemy.Get(), TEXT("nearest enemy chosen without a priority"));
				PC->IssueTargetedShot(State.FarEnemy.Get());
				Check(State, Commander->GetManualPriorityTarget() == State.FarEnemy.Get(), TEXT("far enemy set as priority target"));
				Check(State, Commander->FindBestCombatTarget() == State.FarEnemy.Get(), TEXT("priority target chosen over the nearer enemy"));
				State.NearEnemy->Destroy();
				State.FarEnemy->Destroy();
				// Start a wave so the tactical pause is available.
				Flow->TriggerCombatZone();
				Flow->FinishCutscene();
				Flow->FinishPreparation();
				PC->SpacePressed();
				Next(State);
			}
			break;
		case 5:
			PC->SpaceReleased();
			Check(State, Flow->GetCombatMode() == ECodexCombatMode::TacticalPause, TEXT("tactical pause started"));
			State.PauseBarrel = SpawnAt<ABarrelActor>(World, Commander, Forward - Right, TargetDistance, 60.f);
			PC->IssueTargetedShot(State.PauseBarrel.Get());
			Check(State, State.PauseBarrel.IsValid() && !State.PauseBarrel->IsBurning(), TEXT("pause: barrel shot only planned"));
			Check(State, Commander->GetPlannedTargetedShotCount() == 1, TEXT("one planned shot"));
			Check(State, World->GetSubsystem<UCombatFeedbackSubsystem>()->GetPlannedMarkerCount() == 1, TEXT("plan marker shown"));
			Next(State);
			break;
		case 6:
			if (State.StageTime >= 0.5f)
			{
				PC->SpacePressed();
				Next(State);
			}
			break;
		case 7:
			PC->SpaceReleased();
			Check(State, Flow->GetCombatMode() == ECodexCombatMode::RealTime, TEXT("pause released"));
			Next(State);
			break;
		case 8:
			Check(State, State.PauseBarrel.IsValid() && State.PauseBarrel->IsBurning(), TEXT("planned barrel shot fired on release"));
			Check(State, Commander->GetPlannedTargetedShotCount() == 0, TEXT("plan consumed"));
			Check(State, World->GetSubsystem<UCombatFeedbackSubsystem>()->GetPlannedMarkerCount() == 0, TEXT("plan markers cleared"));
			Next(State);
			Finish(State);
			return false;
		default:
			return false;
		}
		return true;
	}

	void Start(UWorld* World)
	{
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
		AOperativeCharacter* Commander = Squad && Squad->GetMembers().Num() > 0 ? Squad->GetMembers()[0] : nullptr;
		TSharedRef<FState> State = MakeShared<FState>();
		if (!Commander || !Waves)
		{
			Finish(*State);
			return;
		}
		const FVector Forward = Commander->GetActorForwardVector();
		const FVector Right = Commander->GetActorRightVector();
		State->Barrel = SpawnAt<ABarrelActor>(World, Commander, Forward, TargetDistance, 60.f);
		State->Mine = SpawnAt<AProximityMineActor>(World, Commander, Right, TargetDistance, 20.f);
		State->Crate = SpawnAt<ALootCrateActor>(World, Commander, -Right, TargetDistance, 40.f);
		State->PlainCrate = SpawnAt<ALootCrateActor>(World, Commander, -Right - Forward, TargetDistance, 40.f);
		State->Barricade = SpawnAt<ABarricadeActor>(World, Commander, Right - Forward, TargetDistance, 50.f);
		if (State->Mine.IsValid())
		{
			State->Mine->bTrapped = true;
		}
		if (State->Crate.IsValid())
		{
			State->Crate->bTrapped = true;
		}
		if (State->Barricade.IsValid())
		{
			State->Barricade->bTrapped = true;
		}
		const FVector Back = Feet(Commander) - Forward * 100.f + FVector(0.f, 0.f, 100.f);
		State->NearEnemy = Waves->SpawnEnemy(EEnemyArchetype::Brute, Back - Forward * 500.f);
		State->FarEnemy = Waves->SpawnEnemy(EEnemyArchetype::Brute, Back - Forward * 1000.f - Right * 150.f);

		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			UWorld* W = WeakWorld.Get();
			return W && Step(W, *State);
		}), StepSeconds);
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
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakWorld]()
		{
			if (UWorld* W = WeakWorld.Get())
			{
				Start(W);
			}
		}), NavWarmupSeconds, false);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.TargetedShotSmoke"),
		TEXT("Dev check: Ctrl + click shots at a barrel, mine, crate, trapped barricade, priority enemy and a planned pause shot; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
