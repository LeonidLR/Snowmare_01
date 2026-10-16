// Dev-only headless check of the rev 5h trap hold (game designer play-test 2026-10-15, every level) on L_MovementTest:
//   Scripts/smoke_parallel.ps1 -Commands TrapSoloSmoke
// "A spotted mine stops the squad, but the next click moved everyone again in formation and someone stepped on it":
//  0. exploring, the whole squad box-selected, a hidden level mine 11 m ahead (2.5 m beside the way), a ground click through the
//     controller's real path (HandleWorldHit) sends the group off;
//  1. the mine is spotted: every operative stopped, the squad in Solo (a trap Solo), the group selection dropped to the selected
//     operative, the feed line "Mine spotted - squad holding. Move them one by one", TrapSpotted raised;
//  2. the next click moves ONLY the selected operative (the others hold);
//  3. the trap Solo has no 25 m leash (a leash regroup would walk the formation through the trap): the selected one 26 m away,
//     still Solo; B (USquadSubsystem::ToggleSoloMode) regroups;
//  4. the AUTO mode (Commander Mode): a second hidden mine is spotted - the leader stops, every order dropped, NO Solo (its paths
//     go round known traps; the formation settles round him as before);
//  5. a hidden PMC tripwire beside the way is spotted the same way: stopped + Solo, "Tripwire spotted - squad holding".
// Nothing is saved. Must not assume a squad body (any leader / spotter).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Core/PlayerCommands.h"
#include "Debug/EventBusProbe.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/ProximityMineActor.h"
#include "Interactables/TripwireActor.h"
#include "NavigationSystem.h"
#include "Subsystems/CodexEventBus.h"
#include "UI/GameMessageSubsystem.h"
#include "UObject/StrongObjectPtr.h"

namespace TrapSoloSmoke
{
	constexpr float StepSeconds = 0.1f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		FVector F = FVector::ForwardVector;
		FVector R = FVector::RightVector;
		TWeakObjectPtr<AProximityMineActor> Mine;
		TWeakObjectPtr<AProximityMineActor> AutoMine;
		TWeakObjectPtr<ATripwireActor> Wire;
		TWeakObjectPtr<AOperativeCharacter> Leader;
		TMap<TWeakObjectPtr<AOperativeCharacter>, FVector> Held;
		FVector LeaderStart = FVector::ZeroVector;
		FVector ClickGoal = FVector::ZeroVector;
		TStrongObjectPtr<UEventBusProbe> Probe;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		State.Probe.Reset();
		FPlatformMisc::RequestExit(false, TEXT("TrapSoloSmoke"));
		return false;
	}

	void Next(FState& State, int32 Stage = -1)
	{
		State.Stage = Stage >= 0 ? Stage : State.Stage + 1;
		State.StageTime = 0.f;
	}

	int32 CountMessages(UWorld* World, const FString& Part)
	{
		int32 Count = 0;
		if (const UGameMessageSubsystem* Messages = World->GetSubsystem<UGameMessageSubsystem>())
		{
			for (const FGameMessage& Message : Messages->GetHistory())
			{
				Count += Message.Text.ToString().Contains(Part) ? 1 : 0;
			}
		}
		return Count;
	}

	/** A plain left click on the ground through the controller's real click path. */
	void ClickGround(ACodexTacticsPlayerController* PC, const FVector& Point)
	{
		FHitResult Hit;
		Hit.ImpactPoint = Point;
		Hit.Location = Point;
		Hit.ImpactNormal = FVector::UpVector;
		Hit.Normal = FVector::UpVector;
		Hit.bBlockingHit = true;
		PC->HandleWorldHit(Hit);
	}

	FVector Feet(const AOperativeCharacter* Operative)
	{
		return Operative->GetActorLocation() - FVector(0.f, 0.f, Operative->GetSimpleCollisionHalfHeight());
	}

	/** A hidden level mine (not the squad's): armed, hidden under the snow until spotted. */
	AProximityMineActor* SpawnHiddenMine(UWorld* World, const FVector& Ground)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AProximityMineActor* Mine = World->SpawnActor<AProximityMineActor>(Ground + FVector(0.f, 0.f, 5.f), FRotator::ZeroRotator, Params);
		if (Mine)
		{
			Mine->ExplosionDamage = 1.f; // never matters if the check works; harmless if it does not
		}
		return Mine;
	}

	bool AllStopped(USquadSubsystem* Squad)
	{
		for (const AOperativeCharacter* Member : Squad->GetMembers())
		{
			if (Member->GetVelocity().Size2D() > 30.f || Member->HasOwnMoveOrder())
			{
				return false;
			}
		}
		return true;
	}

	void RememberHeld(FState& State, USquadSubsystem* Squad)
	{
		State.Held.Reset();
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			State.Held.Add(Member, Member->GetActorLocation());
		}
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		if (State.Time < 3.f)
		{
			return true;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(World->GetFirstPlayerController());
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!Squad || !PC || !Leader || Squad->GetMembers().Num() < 2 || State.Time > 160.f)
		{
			Check(State, false, FString::Printf(TEXT("squad / controller / timeout (stage %d)"), State.Stage));
			return Finish(State, false);
		}
		switch (State.Stage)
		{
		case 0:
		{
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			FNavLocation OnNav;
			if (!(Nav && Nav->ProjectPointToNavigation(Leader->GetActorLocation(), OnNav, FVector(60.f, 60.f, 200.f))) && State.StageTime < 30.f)
			{
				return true;
			}
			State.Probe.Reset(NewObject<UEventBusProbe>());
			State.Probe->ListenGameplayEvents(UCodexEventBus::Get(World));
			State.F = Leader->GetActorForwardVector().GetSafeNormal2D();
			State.R = FVector::CrossProduct(FVector::UpVector, State.F);
			State.Leader = Leader;
			Squad->SetSelectedGroup(Squad->GetMembers(), false);
			Check(State, Squad->HasMultiSelection() && !Squad->IsSoloMode(), TEXT("the whole squad box-selected, no Solo"));
			const FVector Ground = Feet(Leader);
			State.Mine = SpawnHiddenMine(World, Ground + State.F * 1100.f + State.R * 250.f);
			Check(State, State.Mine.IsValid() && !State.Mine->IsRevealed() && State.Mine->IsHiddenTrap(), TEXT("a hidden level mine 11 m ahead, 2.5 m beside the way"));
			ClickGround(PC, SmokeUtils::ClearPoint(World, Leader->GetActorLocation(), Ground + State.F * 2000.f));
			Next(State);
			return true;
		}
		case 1:
		{
			AProximityMineActor* Mine = State.Mine.Get();
			if (Mine && !Mine->IsRevealed() && State.StageTime < 25.f)
			{
				return true;
			}
			Check(State, Mine && Mine->IsRevealed(), FString::Printf(TEXT("the mine is spotted on the walk (%.1f s)"), State.StageTime));
			Next(State);
			return true;
		}
		case 2:
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			Check(State, AllStopped(Squad), TEXT("every operative stopped (no move order, standing still)"));
			Check(State, Squad->IsSoloMode() && Squad->IsTrapSolo(), TEXT("the squad switched to Solo (a trap Solo)"));
			Check(State, !Squad->HasMultiSelection() && Squad->GetSelectedGroup().Num() == 1 && Squad->GetSelectedGroup()[0] == Squad->GetLeader(),
				TEXT("the box selection dropped to the selected operative"));
			Check(State, CountMessages(World, TEXT("Mine spotted - squad holding. Move them one by one")) == 1, TEXT("feed: 'Mine spotted - squad holding. Move them one by one'"));
			Check(State, State.Probe->CountEvent(GameplayEvents::TrapSpotted) == 1, TEXT("TrapSpotted raised once"));
			// The next order: a click 4 m to the side away from the mine.
			RememberHeld(State, Squad);
			State.LeaderStart = Leader->GetActorLocation();
			State.ClickGoal = SmokeUtils::ClearPoint(World, Leader->GetActorLocation(), Feet(Leader) - State.R * 400.f + State.F * 100.f);
			ClickGround(PC, State.ClickGoal);
			Next(State);
			return true;
		}
		case 3:
		{
			if (FVector::Dist2D(Leader->GetActorLocation(), State.ClickGoal) > 120.f && State.StageTime < 10.f)
			{
				return true;
			}
			float FollowersMoved = 0.f;
			for (const TPair<TWeakObjectPtr<AOperativeCharacter>, FVector>& Entry : State.Held)
			{
				if (Entry.Key.Get() && Entry.Key.Get() != Leader)
				{
					FollowersMoved = FMath::Max(FollowersMoved, static_cast<float>(FVector::Dist2D(Entry.Key->GetActorLocation(), Entry.Value)));
				}
			}
			Check(State, FVector::Dist2D(Leader->GetActorLocation(), State.ClickGoal) <= 120.f,
				FString::Printf(TEXT("the next click moved the selected operative (%.0f cm from the goal)"), FVector::Dist2D(Leader->GetActorLocation(), State.ClickGoal)));
			Check(State, FollowersMoved < 40.f && Squad->IsSoloMode(), FString::Printf(TEXT("... and only him: the others held (moved at most %.0f cm)"), FollowersMoved));
			// No leash in a trap Solo: the selected one 14 m ahead, the others 14 m back (teleports; nobody walks near the mine).
			Leader->TeleportTo(SmokeUtils::FreeSpot(World, Leader->GetActorLocation() + State.F * 1400.f, Leader), Leader->GetActorRotation(), false, true);
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (Member != Leader)
				{
					Member->TeleportTo(SmokeUtils::FreeSpot(World, Member->GetActorLocation() - State.F * 1400.f, Member), Member->GetActorRotation(), false, true);
				}
			}
			Next(State);
			return true;
		}
		case 4:
		{
			if (State.StageTime < 1.f)
			{
				return true;
			}
			float MaxDistance = 0.f;
			for (const AOperativeCharacter* Member : Squad->GetMembers())
			{
				MaxDistance = FMath::Max(MaxDistance, static_cast<float>(FVector::Dist2D(Member->GetActorLocation(), Leader->GetActorLocation())));
			}
			if (MaxDistance > USquadSubsystem::SoloModeMaxDistance)
			{
				Check(State, Squad->IsSoloMode() && Squad->IsTrapSolo(), FString::Printf(TEXT("%.1f m apart: the trap Solo has no leash"), MaxDistance / 100.f));
			}
			else
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: no free spots 26 m apart (%.1f m) - leash check skipped"), MaxDistance / 100.f);
			}
			Squad->ToggleSoloMode(); // B
			Check(State, !Squad->IsSoloMode() && !Squad->IsTrapSolo(), TEXT("B regroups: Solo off"));
			// Back together for the AUTO pass; the first mine goes (a follower's line would cross it).
			if (State.Mine.IsValid())
			{
				State.Mine->Destroy();
			}
			SmokeUtils::PlaceSquadAtTestStart(World);
			Next(State);
			return true;
		}
		case 5:
		{
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			Squad->SetAutonomousSquadCombat(true);
			const FVector Ground = Feet(Leader);
			State.F = Leader->GetActorForwardVector().GetSafeNormal2D();
			State.R = FVector::CrossProduct(FVector::UpVector, State.F);
			State.AutoMine = SpawnHiddenMine(World, Ground + State.F * 1100.f - State.R * 250.f);
			ClickGround(PC, SmokeUtils::ClearPoint(World, Leader->GetActorLocation(), Ground + State.F * 2000.f));
			Next(State);
			return true;
		}
		case 6:
		{
			AProximityMineActor* Mine = State.AutoMine.Get();
			if (Mine && !Mine->IsRevealed() && State.StageTime < 25.f)
			{
				return true;
			}
			Check(State, Mine && Mine->IsRevealed(), FString::Printf(TEXT("AUTO: the second mine is spotted (%.1f s)"), State.StageTime));
			Next(State);
			return true;
		}
		case 7:
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			// AUTO keeps the old rule: everyone's order is dropped, the formation settles round the stopped leader (its paths avoid
			// the known mine), no Solo.
			bool bOrdersDropped = true;
			for (const AOperativeCharacter* Member : Squad->GetMembers())
			{
				bOrdersDropped &= !Member->HasOwnMoveOrder();
			}
			Check(State, bOrdersDropped && Leader->GetVelocity().Size2D() < 30.f && !Squad->IsSoloMode(),
				TEXT("AUTO: the leader stopped, every move order dropped, NO Solo (AUTO paths go round known traps)"));
			Squad->SetAutonomousSquadCombat(false);
			if (State.AutoMine.IsValid())
			{
				State.AutoMine->Destroy();
			}
			SmokeUtils::PlaceSquadAtTestStart(World);
			Next(State);
			return true;
		}
		case 8:
		{
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			const FVector Ground = Feet(Leader);
			State.F = Leader->GetActorForwardVector().GetSafeNormal2D();
			State.R = FVector::CrossProduct(FVector::UpVector, State.F);
			// A hidden PMC wire parallel to the way, 4 m beside it (never crossed, inside the 4.5 m spot range of the leader).
			const FVector Centre = Ground + State.F * 1100.f + State.R * 400.f;
			const FTransform Transform(State.F.Rotation(), Centre + FVector(0.f, 0.f, 5.f));
			APMCTripwireActor* Wire = World->SpawnActorDeferred<APMCTripwireActor>(APMCTripwireActor::StaticClass(), Transform, nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (Wire)
			{
				Wire->AnchorA = FVector(-150.f, 0.f, 0.f);
				Wire->AnchorB = FVector(150.f, 0.f, 0.f);
				Wire->FinishSpawning(Transform);
			}
			State.Wire = Wire;
			ClickGround(PC, SmokeUtils::ClearPoint(World, Leader->GetActorLocation(), Ground + State.F * 2000.f));
			Next(State);
			return true;
		}
		case 9:
		{
			ATripwireActor* Wire = State.Wire.Get();
			if (Wire && Wire->IsConcealed() && State.StageTime < 25.f)
			{
				return true;
			}
			Check(State, Wire && Wire->IsSpotted() && Wire->IsArmed(), FString::Printf(TEXT("the hidden tripwire beside the way is spotted (%.1f s)"), State.StageTime));
			Next(State);
			return true;
		}
		case 10:
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			Check(State, AllStopped(Squad) && Squad->IsSoloMode() && Squad->IsTrapSolo(), TEXT("tripwire: everyone stopped, Solo on"));
			Check(State, CountMessages(World, TEXT("Tripwire spotted - squad holding")) == 1, TEXT("feed: 'Tripwire spotted - squad holding'"));
			Check(State, State.Probe->CountEvent(GameplayEvents::TrapSpotted) == 3 && State.Probe->CountEvent(GameplayEvents::TripwireSpotted) == 1,
				FString::Printf(TEXT("TrapSpotted 3 times in all (2 mines + the wire), TripwireSpotted once (%d / %d)"),
					State.Probe->CountEvent(GameplayEvents::TrapSpotted), State.Probe->CountEvent(GameplayEvents::TripwireSpotted)));
			return Finish(State, true);
		}
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
		FTimerHandle PlaceHandle;
		TWeakObjectPtr<UWorld> PlaceWorld(World);
		World->GetTimerManager().SetTimer(PlaceHandle, FTimerDelegate::CreateLambda([PlaceWorld]()
		{
			SmokeUtils::PlaceSquadAtTestStart(PlaceWorld.Get());
		}), 0.5f, false);
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.TrapSoloSmoke"),
		TEXT("Dev check (rev 5h): a spotted hidden mine / tripwire stops the squad and switches it to Solo (not in AUTO); the next click moves only the selected one."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
