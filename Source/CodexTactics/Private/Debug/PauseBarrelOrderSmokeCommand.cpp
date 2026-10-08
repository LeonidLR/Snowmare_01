// Dev-only headless check of barrel / barricade orders given in the fight (bug + user decision 2026-10-08) on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.PauseBarrelOrderSmoke
// User report: in the real-time fight, tap Space (tactical pause), order an operative to ignite a barrel with matches or
// to push a barrel, tap Space again: nothing happened. Causes: the carry started on the release was dropped the next frame
// by the «no relocation in live combat» check, a planned Ctrl + click shot was lost when the shooter could not fire at the
// release (reloading), and the barrel menu ("Ignite") could not be reached in a fight at all. Drives the controller like
// the mouse / keyboard (HandleWorldHit, the action menu buttons, SpacePressed / SpaceReleased):
//   real-time fight -> tap: pause -> A (empty clip) plans a Ctrl + click shot at barrel 1; B clicks barrel 2 (menu) ->
//   "Push" -> spot; C clicks barrel 3 (menu) -> "Ignite" (planned, marker) -> paused: nothing happens -> tap ->
//   barrel 1 explodes once A reloaded, barrel 2 stands on the spot, C walks up and lights barrel 3 (one match) ->
//   tap: pause -> B clicks the barricade (menu) -> "Relocate" -> spot -> tap -> the barricade stands there ->
//   real time: C clicks barrel 4 -> "Ignite" -> he walks up and lights it; A's Ctrl + click ignites barrel 5 at once; the
//   real-time barrel menu offers no "Push".

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/CombatFeedbackSubsystem.h"
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

namespace PauseBarrelOrderSmoke
{
	constexpr float NavWarmupSeconds = 3.f;
	constexpr float StepSeconds = 0.25f;
	/** Outside the 5.5 m blast of a shot barrel, so the squad is not hurt. */
	constexpr float ShotDistance = 900.f;
	/** Barrel half height (box extent Z). */
	constexpr float BarrelHalfHeight = 70.f;
	constexpr float ResultTimeout = 30.f;
	/** Placed object within this of the marked spot, cm. */
	constexpr float PlaceTolerance = 120.f;

	struct FState
	{
		int32 Stage = 0;
		float StageTime = 0.f;
		int32 Failures = 0;
		FVector SquadStart = FVector::ZeroVector;
		FVector CarryTarget = FVector::ZeroVector;
		FVector CarryStart = FVector::ZeroVector;
		FVector BarricadeTarget = FVector::ZeroVector;
		int32 MatchesBefore = 0;
		TWeakObjectPtr<AOperativeCharacter> Shooter;
		TWeakObjectPtr<AOperativeCharacter> Carrier;
		TWeakObjectPtr<AOperativeCharacter> Lighter;
		TWeakObjectPtr<ABarrelActor> ShotBarrel;
		TWeakObjectPtr<ABarrelActor> CarryBarrel;
		TWeakObjectPtr<ABarrelActor> MatchBarrel;
		TWeakObjectPtr<ABarrelActor> LiveMatchBarrel;
		TWeakObjectPtr<ABarrelActor> LiveShotBarrel;
		TWeakObjectPtr<ABarricadeActor> Barricade;
		bool bShot = false;
		bool bCarried = false;
		bool bLit = false;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("PauseBarrelOrderSmoke"));
		return false;
	}

	void NextStage(FState& State)
	{
		++State.Stage;
		State.StageTime = 0.f;
	}

	FVector Feet(const AOperativeCharacter& Operative)
	{
		return Operative.GetActorLocation() - FVector(0.f, 0.f, Operative.GetSimpleCollisionHalfHeight());
	}

	/** An actor of class T on a reachable ground spot Distance cm from Operative along Direction, HalfHeight above it. */
	template <typename T>
	T* SpawnNear(UWorld* World, const AOperativeCharacter& Operative, const FVector& Direction, float Distance, float HalfHeight)
	{
		const FVector From = Feet(Operative);
		const FVector Spot = SmokeUtils::ClearPoint(World, From, From + Direction.GetSafeNormal2D() * Distance);
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<T>(FVector(Spot.X, Spot.Y, From.Z + HalfHeight), FRotator::ZeroRotator, Params);
	}

	/** Plain left click on Actor through the controller's world-click path. */
	void ClickActor(ACodexTacticsPlayerController& PC, AActor* Actor)
	{
		PC.HandleWorldHit(FHitResult(Actor, nullptr, Actor->GetActorLocation(), FVector::UpVector));
	}

	/** A free spot Distance cm from Worker, turned Turn degrees from the direction to Object. */
	FVector SpotBeside(UWorld* World, const AOperativeCharacter& Worker, const AActor& Object, float Turn, float Distance)
	{
		const FVector From = Feet(Worker);
		const FVector Away = (Object.GetActorLocation() - Worker.GetActorLocation()).GetSafeNormal2D();
		return SmokeUtils::ClearPoint(World, From, From + Away.RotateAngleAxis(Turn, FVector::UpVector) * Distance);
	}

	bool Step(UWorld* World, FState& State)
	{
		State.StageTime += StepSeconds;
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		URelocationSubsystem* Relocation = World->GetSubsystem<URelocationSubsystem>();
		UInteractionSubsystem* Interactions = World->GetSubsystem<UInteractionSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		if (!Flow || !Squad || !Relocation || !Interactions || !PC || Squad->GetMembers().Num() < 3)
		{
			Check(State, false, TEXT("flow, squad (3+), relocation, interactions and controller"));
			return Finish(State, false);
		}
		// The level wave is parked frozen 40 m away: nothing interrupts the orders.
		if (State.Stage > 0)
		{
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->CustomTimeDilation = 0.f;
				if (FVector::Dist2D(It->GetActorLocation(), State.SquadStart) < 3000.f)
				{
					It->SetActorLocation(State.SquadStart + FVector(4000.f, 4000.f, 2000.f), false, nullptr, ETeleportType::TeleportPhysics);
				}
			}
		}
		AOperativeCharacter* Shooter = State.Shooter.Get();
		AOperativeCharacter* Carrier = State.Carrier.Get();
		AOperativeCharacter* Lighter = State.Lighter.Get();
		if (State.Stage > 0 && (!Shooter || !Carrier || !Lighter || !State.ShotBarrel.IsValid() || !State.CarryBarrel.IsValid()
			|| !State.MatchBarrel.IsValid() || !State.LiveMatchBarrel.IsValid() || !State.LiveShotBarrel.IsValid() || !State.Barricade.IsValid()))
		{
			Check(State, false, TEXT("operatives and objects still valid"));
			return Finish(State, false);
		}

		switch (State.Stage)
		{
		case 0: // Real-time fight; the objects around the squad.
		{
			Flow->TriggerCombatZone();
			Flow->FinishCutscene();
			Flow->FinishPreparation();
			State.Shooter = Squad->GetMembers()[0];
			State.Carrier = Squad->GetMembers()[1];
			State.Lighter = Squad->GetMembers()[2];
			Shooter = State.Shooter.Get();
			Squad->SetLeader(Shooter);
			State.SquadStart = Shooter->GetActorLocation();
			const FVector Forward = Shooter->GetActorForwardVector();
			const FVector Right = Shooter->GetActorRightVector();
			State.ShotBarrel = SpawnNear<ABarrelActor>(World, *Shooter, Forward, ShotDistance, BarrelHalfHeight);
			State.LiveShotBarrel = SpawnNear<ABarrelActor>(World, *Shooter, Right, ShotDistance, BarrelHalfHeight);
			State.CarryBarrel = SpawnNear<ABarrelActor>(World, *State.Carrier, -Forward - Right * 0.5f, 350.f, BarrelHalfHeight);
			State.Barricade = SpawnNear<ABarricadeActor>(World, *State.Carrier, -Forward + Right * 0.5f, 350.f, 50.f);
			State.MatchBarrel = SpawnNear<ABarrelActor>(World, *State.Lighter, -Right, 500.f, BarrelHalfHeight);
			State.LiveMatchBarrel = SpawnNear<ABarrelActor>(World, *State.Lighter, -Forward - Right, 500.f, BarrelHalfHeight);
			Check(State, State.ShotBarrel.IsValid() && State.LiveShotBarrel.IsValid() && State.CarryBarrel.IsValid() && State.Barricade.IsValid()
				&& State.MatchBarrel.IsValid() && State.LiveMatchBarrel.IsValid(), TEXT("five barrels and a barricade spawned"));
			Check(State, Flow->GetPhase() == ECodexGamePhase::WaveCombat && Flow->GetCombatMode() == ECodexCombatMode::RealTime,
				TEXT("the fight runs in real time"));
			PC->SpacePressed();
			NextStage(State);
			break;
		}
		case 1: // Tap -> tactical pause; plan the orders.
		{
			PC->SpaceReleased();
			Check(State, Flow->GetCombatMode() == ECodexCombatMode::TacticalPause, TEXT("Space tap: tactical pause"));
			// A: the fight emptied his clip - at the release he must reload before the shot (the lost-shot case).
			Shooter->CurrentClip = 0;
			Shooter->ReserveAmmo = FMath::Max(Shooter->ReserveAmmo, 60);
			PC->IssueTargetedShot(State.ShotBarrel.Get()); // Ctrl + click on the barrel
			Check(State, Shooter->GetPlannedTargetedShotCount() == 1, TEXT("A: Ctrl + click shot at barrel 1 planned"));
			// B: click on barrel 2 -> its menu -> "Push" -> click on the spot.
			Squad->SetLeader(Carrier);
			ClickActor(*PC, State.CarryBarrel.Get());
			Check(State, Interactions->IsActionMenuOpen() && Interactions->GetActionMenu().bAllowRelocate,
				TEXT("pause: barrel click opens its menu with \"Push\""));
			Interactions->RelocateActionMenu();
			Check(State, Relocation->IsPlacing(), TEXT("B: \"Push\" starts the placement"));
			State.CarryStart = State.CarryBarrel->GetActorLocation();
			State.CarryTarget = SpotBeside(World, *Carrier, *State.CarryBarrel, 60.f, 600.f);
			Relocation->ConfirmPlacement(State.CarryTarget);
			Check(State, Relocation->HasPlannedTask(Carrier) && Relocation->GetActiveTaskCount() == 0, TEXT("B: push planned, not started"));
			// C: click on barrel 3 -> its menu -> «Разжечь (1 спичка)».
			Lighter->MatchesCount = FMath::Max(Lighter->MatchesCount, 3);
			State.MatchesBefore = Lighter->MatchesCount;
			Squad->SetLeader(Lighter);
			ClickActor(*PC, State.MatchBarrel.Get());
			Check(State, Interactions->IsActionMenuOpen() && Interactions->GetMenuTarget() == State.MatchBarrel.Get()
				&& !Interactions->GetActionMenu().bConfirmDisabled, TEXT("pause: barrel 3 menu with \"Ignite\" enabled"));
			Interactions->ConfirmActionMenu();
			Check(State, Interactions->GetPlannedUseOrderCount() == 1 && !State.MatchBarrel->IsBurning()
				&& Lighter->MatchesCount == State.MatchesBefore, TEXT("C: \"Ignite\" planned (no match spent yet)"));
			NextStage(State);
			break;
		}
		case 2:
			if (State.StageTime >= 1.f)
			{
				Check(State, !State.ShotBarrel->IsBurning() && !State.MatchBarrel->IsBurning(), TEXT("paused: no barrel burns yet"));
				Check(State, FVector::Dist2D(State.CarryBarrel->GetActorLocation(), State.CarryStart) < 10.f, TEXT("paused: barrel 2 still in place"));
				Check(State, World->GetSubsystem<UCombatFeedbackSubsystem>()->GetPlannedMarkerCount() >= 3, TEXT("plan markers shown"));
				PC->SpacePressed();
				NextStage(State);
			}
			break;
		case 3: // Tap -> real time; the queued orders run.
			PC->SpaceReleased();
			Check(State, Flow->GetCombatMode() == ECodexCombatMode::RealTime, TEXT("second tap: real time"));
			Check(State, Relocation->GetActiveTaskCount() == 1, TEXT("release: the push task started"));
			Check(State, Squad->GetDeferredShotCount() == 1 || State.ShotBarrel->IsBurning(), TEXT("release: the shot fired or waits for the reload"));
			Check(State, Interactions->GetActiveUseOrderCount() == 1 || State.MatchBarrel->IsBurning(), TEXT("release: C walks up to light barrel 3"));
			NextStage(State);
			break;
		case 4: // All three orders finish in real time.
		{
			if (!State.bShot && State.ShotBarrel->IsBurning())
			{
				State.bShot = true;
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke diag: barrel 1 shot %.2f s after the release"), State.StageTime);
			}
			if (!State.bLit && State.MatchBarrel->IsBurning())
			{
				State.bLit = true;
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke diag: barrel 3 lit %.2f s after the release"), State.StageTime);
			}
			const float CarryError = FVector::Dist2D(State.CarryBarrel->GetActorLocation(), State.CarryTarget);
			if (!State.bCarried && Relocation->GetActiveTaskCount() == 0)
			{
				State.bCarried = true;
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke diag: push finished %.2f s after the release, %.0f cm from the spot"), State.StageTime, CarryError);
			}
			if ((State.bShot && State.bLit && State.bCarried) || State.StageTime >= ResultTimeout)
			{
				Check(State, State.bShot && Squad->GetDeferredShotCount() == 0, TEXT("A's paused shot ran after the reload: barrel 1 burns"));
				Check(State, State.bCarried && CarryError < PlaceTolerance, TEXT("B's paused push ran: barrel 2 stands on the marked spot"));
				Check(State, !Carrier->bCarrying, TEXT("B let go of it"));
				Check(State, State.bLit && Lighter->MatchesCount == State.MatchesBefore - 1, TEXT("C's paused \"Ignite\" ran: barrel 3 burns, one match spent"));
				Check(State, Interactions->GetActiveUseOrderCount() == 0, TEXT("no use order left"));
				PC->SpacePressed();
				NextStage(State);
			}
			break;
		}
		case 5: // Second pause: B moves the barricade through its menu.
			PC->SpaceReleased();
			Check(State, Flow->GetCombatMode() == ECodexCombatMode::TacticalPause, TEXT("tap: tactical pause again"));
			Squad->SetLeader(Carrier);
			ClickActor(*PC, State.Barricade.Get());
			Check(State, Interactions->IsActionMenuOpen() && Interactions->GetActionMenu().bAllowRelocate, TEXT("pause: barricade menu with \"Relocate\""));
			Interactions->RelocateActionMenu();
			Check(State, Relocation->IsPlacing(), TEXT("B: \"Relocate\" starts the barricade placement"));
			State.BarricadeTarget = SpotBeside(World, *Carrier, *State.Barricade, -70.f, 500.f);
			Relocation->ConfirmPlacement(State.BarricadeTarget);
			Check(State, Relocation->HasPlannedTask(Carrier), TEXT("B: barricade move planned"));
			PC->SpacePressed();
			NextStage(State);
			break;
		case 6:
			PC->SpaceReleased();
			Check(State, Flow->GetCombatMode() == ECodexCombatMode::RealTime && Relocation->GetActiveTaskCount() == 1,
				TEXT("tap: real time, the barricade move started"));
			NextStage(State);
			break;
		case 7:
			if (Relocation->GetActiveTaskCount() == 0 || State.StageTime >= ResultTimeout)
			{
				const float Error = FVector::Dist2D(State.Barricade->GetActorLocation(), State.BarricadeTarget);
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke diag: barricade move %.2f s, %.0f cm from the spot"), State.StageTime, Error);
				Check(State, Relocation->GetActiveTaskCount() == 0 && Error < PlaceTolerance, TEXT("the paused barricade move ran: it stands on the spot"));
				// Real time: C lights barrel 4 through its menu.
				State.MatchesBefore = Lighter->MatchesCount;
				Squad->SetLeader(Lighter);
				ClickActor(*PC, State.LiveMatchBarrel.Get());
				Check(State, Interactions->IsActionMenuOpen() && !Interactions->GetActionMenu().bAllowRelocate,
					TEXT("real time: barrel menu with \"Ignite\", no \"Push\""));
				Interactions->ConfirmActionMenu();
				Check(State, Interactions->GetActiveUseOrderCount() == 1 || State.LiveMatchBarrel->IsBurning(), TEXT("real time: C walks up at once"));
				NextStage(State);
			}
			break;
		case 8:
			if (State.LiveMatchBarrel->IsBurning() || State.StageTime >= ResultTimeout)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke diag: barrel 4 lit after %.2f s"), State.StageTime);
				Check(State, State.LiveMatchBarrel->IsBurning() && Lighter->MatchesCount == State.MatchesBefore - 1,
					TEXT("real time: \"Ignite\" ran, barrel 4 burns, one match spent"));
				// Real time without a pause: the Ctrl + click shot ignites at once.
				Squad->SetLeader(Shooter);
				Shooter->CurrentClip = FMath::Max(Shooter->CurrentClip, 5);
				PC->IssueTargetedShot(State.LiveShotBarrel.Get());
				Check(State, State.LiveShotBarrel->IsBurning(), TEXT("real time: the Ctrl + click shot ignites barrel 5 at once"));
				return Finish(State, true);
			}
			break;
		default:
			return Finish(State, false);
		}
		return true;
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
			// Real-time ticker: world timers crawl during the tactical pause.
			TSharedRef<FState> State = MakeShared<FState>();
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
			{
				UWorld* W = WeakWorld.Get();
				return W && Step(W, *State);
			}), StepSeconds);
		}), NavWarmupSeconds, false);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.PauseBarrelOrderSmoke"),
		TEXT("Dev check of barrel / barricade orders in the fight: paused shot (after a reload), push, barricade move and \"Ignite\" run on resume; real-time \"Ignite\"; PASS / FAIL."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
