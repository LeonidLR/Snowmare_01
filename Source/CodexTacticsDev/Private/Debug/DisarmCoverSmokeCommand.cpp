// Dev-only check of two game designer bugs (2026-10-21) on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.DisarmCoverSmoke
//  1. "In cover, pressed to the wall, an order to disarm a tripwire does nothing": a spotted PMC wire lies ~55 cm from the cover slot
//     (inside the disarm reach, so the menu used to open at once with no move order and he stayed in the cover). Now the click takes him off
//     the wall, the menu opens facing the wire at the disarm spot, Disarm works (crouched, TripwireDisarmed). A wire farther away: he leaves
//     the cover and walks up. The menu opened in cover by another path (the pause, OpenMenuNow) + Confirm also takes him off the wall.
//  2. "Clicking the wire while he disarms opens its window, and it stays when he finishes": the window opened during the work closes the
//     moment the wire is disarmed (waiting for its pickup grab), a window on a wire destroyed from under it closes on Cancel / by itself;
//     the open / close broadcasts the HUD listens to are balanced.
// A 14 m x 2.5 m wall is spawned ahead of the squad (nothing is saved); the wires are APMCTripwireActor (spotted, forced successful roll).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Core/PlayerCommands.h"
#include "Debug/EventBusProbe.h"
#include "Debug/SmokeUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Interactables/InteractionSubsystem.h"
#include "Interactables/TripwireActor.h"
#include "Subsystems/CodexEventBus.h"
#include "Tactics/CoverTraceRules.h"
#include "Tutorial/TutorialSubsystem.h"

namespace DisarmCoverSmoke
{
	constexpr float StepSeconds = 0.1f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		FVector Ground = FVector::ZeroVector;
		FVector F = FVector::ForwardVector;
		FVector R = FVector::RightVector;
		FVector Centre = FVector::ZeroVector;
		TWeakObjectPtr<AOperativeCharacter> Op;
		TWeakObjectPtr<ATripwireActor> Wire;
		TStrongObjectPtr<UEventBusProbe> Probe;
		float WireR = 0.f;
		int32 ClosesBefore = 0;
		bool bAwaitSeen = false;
		bool bGoneAtOnce = false;
		float AwaitAt = 0.f;
		bool bRequested = false;
		TArray<TWeakObjectPtr<AActor>> Spawned;
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
		FPlatformMisc::RequestExit(false, TEXT("DisarmCoverSmoke"));
		return false;
	}

	void Next(FState& State, int32 Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
		State.bRequested = false;
	}

	bool Settled(const AOperativeCharacter* Op)
	{
		return Op && Op->bInCover && !Op->bShimmying && !Op->IsMoving() && Op->GetVelocity().Size2D() < 5.f && Op->GetCoverEntryMeshOffset().Size() < 1.f;
	}

	void Place(AOperativeCharacter& Op, const FVector& Ground)
	{
		Op.StopOperative();
		Op.LeaveCover(TEXT("smoke reset"));
		Op.SetStance(EOperativeStance::Standing);
		Op.TeleportTo(Ground + FVector(0.f, 0.f, Op.GetSimpleCollisionHalfHeight() + 5.f), Op.GetActorRotation(), false, true);
	}

	AStaticMeshActor* SpawnWall(UWorld* World, const FVector& Centre, const FRotator& Rotation, const FVector& Scale)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		const FTransform Transform(Rotation, Centre, Scale);
		AStaticMeshActor* Block = Cube ? World->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn) : nullptr;
		if (!Block)
		{
			return nullptr;
		}
		Block->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Block->GetStaticMeshComponent()->SetStaticMesh(Cube);
		Block->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
		Block->GetStaticMeshComponent()->SetCanEverAffectNavigation(true);
		Block->FinishSpawning(Transform);
		return Block;
	}

	/** A spotted PMC wire across R at Centre + F * Forward + R * Right (anchors 1.5 m either side), success rolls forced. */
	ATripwireActor* SpawnWire(UWorld* World, const FState& State, float Forward, float Right)
	{
		const FVector Where = State.Centre + State.F * Forward + State.R * Right;
		const FTransform Transform(State.R.Rotation(), Where + FVector(0.f, 0.f, 5.f));
		APMCTripwireActor* Wire = World->SpawnActorDeferred<APMCTripwireActor>(APMCTripwireActor::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Wire)
		{
			Wire->AnchorA = FVector(-150.f, 0.f, 0.f);
			Wire->AnchorB = FVector(150.f, 0.f, 0.f);
			Wire->FinishSpawning(Transform);
			Wire->Reveal();
			Wire->DefusalRollForTesting = 0.f;
			Wire->DefusalExplosionRollForTesting = 99.f;
		}
		return Wire;
	}

	/** He takes cover at the wall's face (RightOffset along it) from 35 cm away. */
	bool OrderCoverAtWall(UWorld* World, FState& State, AOperativeCharacter& Op, float RightOffset)
	{
		const FVector Out = State.Centre - State.F * 70.f + State.R * RightOffset; // between the wire and the wall (round the wall's end he would cross the wire)
		Place(Op, FVector(Out.X, Out.Y, State.Ground.Z));
		FCoverSlot Slot;
		const FVector Face = State.Centre - State.F * 75.f + State.R * RightOffset;
		if (!CoverTraceRules::FindCoverSlotAt(World, FVector(Face.X, Face.Y, State.Ground.Z + 60.f), -State.F, Slot))
		{
			return false;
		}
		return Op.OrderTakeCover(Slot, false) == EOperativeOrderResult::Accepted;
	}

	/** At the disarm spot: within 90 cm of the wire line (not on it), facing it (+-30 deg). */
	void CheckSpot(FState& State, const AOperativeCharacter& Op, const ATripwireActor& Wire, const TCHAR* Pass)
	{
		const FVector Focus = Wire.GetDisarmFocusPoint(Op.GetActorLocation());
		const float Distance = static_cast<float>(FVector::Dist2D(Op.GetActorLocation(), Focus));
		const FVector ToWire = (Focus - Op.GetActorLocation()).GetSafeNormal2D();
		const float FacingDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Op.GetActorForwardVector().GetSafeNormal2D(), ToWire), -1.0, 1.0)));
		Check(State, Distance <= 90.f && Distance >= 30.f && FacingDeg <= 30.f, FString::Printf(TEXT("%s: at the disarm spot %.0f cm from the wire line, facing it (%.0f deg)"),
			Pass, Distance, FacingDeg));
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 200.f)
		{
			return World ? Finish(State, false) : false;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UInteractionSubsystem* Interactions = World->GetSubsystem<UInteractionSubsystem>();
		AOperativeCharacter* Op = State.Op.Get();
		ATripwireActor* Wire = State.Wire.Get();
		switch (State.Stage)
		{
		case 0:
		{
			if (State.StageTime < 3.f || !Squad || !Interactions || Squad->GetMembers().Num() < 3)
			{
				return true;
			}
			State.Probe.Reset(NewObject<UEventBusProbe>());
			State.Probe->ListenGameplayEvents(UCodexEventBus::Get(World));
			State.Probe->ListenActionMenu(Interactions);
			if (UTutorialSubsystem* Tutorial = World->GetSubsystem<UTutorialSubsystem>())
			{
				Tutorial->SkipTutorial(); // its locks / confinements are not under test
			}
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				SmokeUtils::ParkEnemyFarAway(*It);
				It->SetActorTickEnabled(false);
			}
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->HealthComponent->SetMaxHealth(100000.f);
				if (Member->SquadRole == EOperativeRole::Engineer)
				{
					State.Op = Member;
				}
			}
			Op = State.Op.Get();
			if (!Op)
			{
				Check(State, false, TEXT("an engineer in the squad"));
				return Finish(State, false);
			}
			const AOperativeCharacter* Leader = Squad->GetLeader();
			State.Ground = Leader->GetActorLocation() - FVector(0.f, 0.f, Leader->GetSimpleCollisionHalfHeight());
			State.F = Leader->GetActorForwardVector().GetSafeNormal2D();
			State.R = FVector::CrossProduct(FVector::UpVector, State.F);
			State.Centre = SmokeUtils::ClearPoint(World, State.Ground, State.Ground + State.F * 900.f);
			State.Centre.Z = State.Ground.Z;
			// Park the others far behind, the engineer leads alone.
			int32 Index = 0;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (Member != Op)
				{
					Place(*Member, State.Ground - State.F * 900.f + State.R * (Index++ * 200.f - 200.f));
				}
			}
			Squad->EnterSoloMode();
			Squad->SetLeader(Op);
			// The wall: 6 m along R, 40 cm thick, 2.5 m high; its face (towards the wire's far side, normal +F) 105 cm before the wire line.
			const FVector WallCentre = State.Centre - State.F * 125.f + FVector(0.f, 0.f, 125.f);
			State.Spawned.Add(SpawnWall(World, FVector(WallCentre.X, WallCentre.Y, State.Ground.Z + 125.f), State.F.Rotation(), FVector(0.4f, 14.f, 2.5f)));
			State.Wire = SpawnWire(World, State, 0.f, 0.f);
			Check(State, State.Wire.IsValid() && State.Spawned[0].IsValid(), TEXT("wall and a spotted PMC wire 105 cm in front of it"));
			Next(State, 1);
			return true;
		}
		case 1:
		{
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			Check(State, OrderCoverAtWall(World, State, *Op, 0.f), TEXT("1: the engineer is ordered into the cover at the wall"));
			Next(State, 2);
			return true;
		}
		case 2:
		{
			if ((!Settled(Op) || State.StageTime < 1.5f) && State.StageTime < 15.f)
			{
				return true;
			}
			const bool bReach = Wire && Wire->IsInInteractionReach(Op->GetActorLocation(), false);
			Check(State, Op->bInCover && bReach, FString::Printf(TEXT("1: pressed to the wall, in cover, and the wire is already inside the disarm reach (%.0f cm from the wire line)"),
				Wire ? FVector::Dist2D(Op->GetActorLocation(), Wire->GetDisarmFocusPoint(Op->GetActorLocation())) : -1.f));
			Interactions->RequestInteraction(Wire); // the click on the wire
			Next(State, 3);
			return true;
		}
		case 3:
		{
			if (!Interactions->IsActionMenuOpen() && State.StageTime < 10.f)
			{
				return true;
			}
			Check(State, Interactions->IsActionMenuOpen() && Interactions->GetMenuTarget() == Wire, FString::Printf(TEXT("1: the Disarm menu opened (%.1f s)"), State.StageTime));
			Check(State, !Op->bInCover, TEXT("1: ... and he left the cover (the order is not dropped)"));
			CheckSpot(State, *Op, *Wire, TEXT("1"));
			Interactions->ConfirmActionMenu();
			Check(State, Wire->IsDisarming() && Op->GetStance() == EOperativeStance::Crouching, FString::Printf(TEXT("1: Disarm: he works on it, crouched (stance %d)"),
				static_cast<int32>(Op->GetStance())));
			Next(State, 4);
			return true;
		}
		case 4:
		{
			// 2: the second click on the wire while he works.
			if (State.StageTime < 1.f)
			{
				return true;
			}
			Interactions->OpenMenuNow(Wire);
			Check(State, Interactions->IsActionMenuOpen() && Interactions->GetActionMenu().bConfirmDisabled, TEXT("2: a second click while he works opens the window with Disarm greyed"));
			State.ClosesBefore = State.Probe->MenuCloses;
			Next(State, 5);
			return true;
		}
		case 5:
		{
			Wire = State.Wire.Get();
			if (Wire && Wire->IsAwaitingPickup() && !State.bAwaitSeen)
			{
				State.bAwaitSeen = true;
				State.AwaitAt = State.StageTime;
			}
			if (!Wire && !State.bAwaitSeen)
			{
				State.bAwaitSeen = true; // gone without a pickup wait: the same grace for the window to close
				State.AwaitAt = State.StageTime;
				State.bGoneAtOnce = true;
			}
			if ((!State.bAwaitSeen || State.StageTime < State.AwaitAt + 0.3f) && State.StageTime < 15.f)
			{
				return true;
			}
			if (State.bAwaitSeen && !State.bGoneAtOnce)
			{
				Check(State, !Interactions->IsActionMenuOpen() && State.Probe->MenuCloses == State.ClosesBefore + 1,
					FString::Printf(TEXT("2: the moment the wire is disarmed (waiting for the pickup grab) the window is closed (%d close broadcast)"),
						State.Probe->MenuCloses - State.ClosesBefore));
			}
			else
			{
				// No pickup clip on the headless run: the wire went in the same breath as the disarm - the window must be closed all the same.
				Check(State, !Interactions->IsActionMenuOpen() && State.Probe->MenuCloses == State.ClosesBefore + 1,
					FString::Printf(TEXT("2: the wire is disarmed and gone (no pickup wait), the window is closed (%d close broadcast)"), State.Probe->MenuCloses - State.ClosesBefore));
			}
			Next(State, 6);
			return true;
		}
		case 6:
		{
			if (State.Wire.IsValid() && State.StageTime < 8.f)
			{
				return true;
			}
			Check(State, !State.Wire.IsValid() && State.Probe->CountEvent(GameplayEvents::TripwireDisarmed) == 1, TEXT("2: the wire is gone after the pickup, TripwireDisarmed once"));
			Check(State, !Interactions->IsActionMenuOpen() && State.Probe->MenuOpens == State.Probe->MenuCloses,
				FString::Printf(TEXT("2: the window stays closed; open / close broadcasts balanced (%d / %d)"), State.Probe->MenuOpens, State.Probe->MenuCloses));
			// A window on a wire that is destroyed under it: Cancel still closes it.
			State.Wire = SpawnWire(World, State, 0.f, 700.f);
			Wire = State.Wire.Get();
			Interactions->OpenMenuNow(Wire);
			const int32 Opens = State.Probe->MenuOpens;
			Check(State, Interactions->IsActionMenuOpen(), TEXT("2: a window on another wire"));
			Wire->Destroy();
			Interactions->CancelActionMenu(); // before the next tick: the target is already garbage
			Check(State, !Interactions->IsActionMenuOpen() && State.Probe->MenuCloses == Opens,
				FString::Printf(TEXT("2: the wire destroyed under the window, Cancel closes it (opens %d, closes %d)"), Opens, State.Probe->MenuCloses));
			// ... and without any click it closes by itself.
			State.Wire = SpawnWire(World, State, 0.f, -700.f);
			Interactions->OpenMenuNow(State.Wire.Get());
			State.Wire->Destroy();
			Next(State, 7);
			return true;
		}
		case 7:
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			Check(State, !Interactions->IsActionMenuOpen() && State.Probe->MenuOpens == State.Probe->MenuCloses,
				FString::Printf(TEXT("2: ... and a window whose wire vanished closes by itself (opens %d, closes %d)"), State.Probe->MenuOpens, State.Probe->MenuCloses));
			// 1b: the menu opened in the cover by another path (the pause, OpenMenuNow), then Confirm.
			State.Wire = SpawnWire(World, State, 0.f, 200.f);
			Check(State, OrderCoverAtWall(World, State, *Op, 200.f), TEXT("1b: the engineer takes cover at the wall 2 m along, the new wire in front of him"));
			Next(State, 8);
			return true;
		}
		case 8:
		{
			if ((!Settled(Op) || State.StageTime < 1.5f) && State.StageTime < 15.f)
			{
				return true;
			}
			Interactions->OpenMenuNow(State.Wire.Get());
			Check(State, Op->bInCover && Interactions->IsActionMenuOpen(), TEXT("1b: the menu is open and he is still in cover (no walk-up in between)"));
			Interactions->ConfirmActionMenu();
			Check(State, !Op->bInCover && State.Wire->IsDisarming(), FString::Printf(TEXT("1b: Confirm takes him off the wall and he disarms (cover %d, disarming %d)"),
				Op->bInCover ? 1 : 0, State.Wire->IsDisarming() ? 1 : 0));
			Next(State, 9);
			return true;
		}
		case 9:
		{
			if (State.Wire.IsValid() && State.StageTime < 10.f)
			{
				return true;
			}
			Check(State, !State.Wire.IsValid(), TEXT("1b: disarmed"));
			// 1c: a wire 3 m from the wall: leave the cover, walk up.
			State.Wire = SpawnWire(World, State, 300.f, 0.f);
			Check(State, OrderCoverAtWall(World, State, *Op, 0.f), TEXT("1c: the engineer takes cover at the wall again, a wire 4 m from it"));
			Next(State, 10);
			return true;
		}
		case 10:
		{
			if ((!Settled(Op) || State.StageTime < 1.5f) && State.StageTime < 15.f)
			{
				return true;
			}
			Wire = State.Wire.Get();
			Check(State, Op->bInCover && Wire && !Wire->IsInInteractionReach(Op->GetActorLocation(), false), TEXT("1c: in cover, the wire out of reach"));
			Interactions->RequestInteraction(Wire);
			Next(State, 11);
			return true;
		}
		case 11:
		{
			if (!Interactions->IsActionMenuOpen() && State.StageTime < 15.f)
			{
				return true;
			}
			Wire = State.Wire.Get();
			Check(State, Interactions->IsActionMenuOpen() && !Op->bInCover, FString::Printf(TEXT("1c: he left the cover, walked up, the menu opened (%.1f s)"), State.StageTime));
			if (Wire)
			{
				CheckSpot(State, *Op, *Wire, TEXT("1c"));
			}
			Interactions->CancelActionMenu();
			Check(State, !Interactions->IsActionMenuOpen() && State.Probe->MenuOpens == State.Probe->MenuCloses, TEXT("Cancel closes the window; broadcasts balanced"));
			return Finish(State, true);
		}
		default:
			return Finish(State, false);
		}
	}

	void Run(UWorld* World)
	{
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorld Command(
		TEXT("CodexTactics.DisarmCoverSmoke"),
		TEXT("Dev check: a disarm order from the cover takes the operative off the wall; the wire's window closes when it is disarmed / destroyed."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
