// Dev-only headless check of the Demo 01 G3 PMC tripwire (a hostile wire placed in the level) on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.PMCTripwireSmoke
// Exploration, nothing saved into the map. An APMCTripwireActor is spawned 6 m ahead of the squad with its two anchors
// (actor space, as the viewport handles) 3 m apart:
//  0. laid between the anchors at BeginPlay, armed at once, hidden (not clickable, no label);
//  1. a frozen hound standing on it: no trip (PMC wires react only to operatives);
//  2. the crouched engineer at 5.5 m does not spot it, the crouched Medic-Sapper at 5.5 m does (4.5 m + 1.5 m): the wire
//     shows (clickable), TripwireSpotted with her as the subject;
//  3. the Commander at 80 % cold: chance = warm chance - 50, disarm takes 5 s; the first attempt only warns (dangerous),
//     the second (forced miss + detonation roll) pulls the pin: blast, the wire is gone, TripwireTriggered;
//  4. a second wire: the Medic-Sapper disarms it in 3 s (forced success): gone, +2 grenades, TripwireDisarmed (rev 2026-10-19: the wire
//     stays, harmless, until the pickup clip's grab; the grenades and TripwireDisarmed come after it - checked standing and crouched);
//  5. a third wire: the engineer lying prone on it - no trip; standing up on it - trip;
//  6. rev 5e (game designer 2026-10-14) known-trap pathing: a spotted wire across the way - the player-control filter's path runs
//     straight through it, the AUTO filter's path goes round; with the squad AUTO mode OFF a move order through the spotted wire
//     is literal (the commander walks into it and trips it), with AUTO ON (Ctrl+T) the same order goes round it (no trip); a
//     spotted mine gets the known-trap area too (literal path over it, AUTO path round it);
//  7. rev 5e (user bug 2026-10-14, "the tripwire cannot be disarmed") the REAL input: a click on the ground 25 cm beside a spotted
//     wire (ACodexTacticsPlayerController::HandleWorldHit) is taken as a click on the wire - the engineer walks up (not into it),
//     the action menu «Disarm» opens, confirming it disarms the wire (TripwireDisarmed, +2 grenades); the label says "click to disarm";
//  7b. 2026-10-15 (game designer): the walk-up ends <= 0.9 m from the wire line, facing it; after the success a standing disarmer stands
//     up and plays the pickup clip on the full body, a crouched one on the upper body (stays crouched), a prone one none;
//  8. save / load: a hidden wire and a spotted wire saved; after revealing the first and removing the second the load
//     restores both (hidden / spotted, armed, PMC).
// Saves go to Saved/SmokeSaves/PMCTripwire (exclusive in Scripts/test_map.json).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Core/PlayerCommands.h"
#include "Interactables/InteractionSubsystem.h"
#include "Core/SaveGameSubsystem.h"
#include "Debug/EventBusProbe.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/ProximityMineActor.h"
#include "Interactables/TripwireActor.h"
#include "Interactables/TripwireRules.h"
#include "Level/CrawlGapActor.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Misc/Paths.h"
#include "Subsystems/CodexEventBus.h"
#include "UI/OverheadLabel.h"
#include "UObject/StrongObjectPtr.h"

namespace PMCTripwireSmoke
{
	struct FState
	{
		float Time = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		FVector Ground = FVector::ZeroVector;
		FVector F = FVector::ForwardVector;
		FVector R = FVector::RightVector;
		FVector Centre = FVector::ZeroVector;
		FVector Second = FVector::ZeroVector;
		TWeakObjectPtr<AOperativeCharacter> Commander;
		TWeakObjectPtr<AOperativeCharacter> Engineer;
		TWeakObjectPtr<AOperativeCharacter> Medic;
		TWeakObjectPtr<ATripwireActor> Wire;
		TWeakObjectPtr<ATripwireActor> Hidden;
		TWeakObjectPtr<ATripwireActor> Spotted;
		FString HiddenName;
		FString SpottedName;
		TWeakObjectPtr<AEnemyCharacter> Hound;
		TMap<TWeakObjectPtr<AOperativeCharacter>, float> Cold;
		float HoundHealth = 0.f;
		float CommanderHealth = 0.f;
		int32 MedicGrenades = 0;
		TWeakObjectPtr<AProximityMineActor> Mine;
		bool bCheckedWorking = false;
		/** 2026-10-15 disarm walk-up / pickup passes. */
		bool bRequested = false;
		int32 PickupsBeforeProne = 0;
		TStrongObjectPtr<UEventBusProbe> Probe;
		/** Rev 2026-10-19: the disarmed wire waiting for the pickup grab (seen when / its delay / what it must not have done yet). */
		bool bAwaitSeen = false;
		bool bAwaitWrong = false;
		float AwaitAt = 0.f;
		float AwaitDelay = 0.f;
		int32 AwaitEvents = 0;
		int32 AwaitGrenades = 0;
	};

	/**
	 * Rev 2026-10-19 (game designer: bend down FIRST, then the trap disappears): while the disarmed wire waits for the pickup clip's
	 * grab it is still there, the grenades are not in the kit and TripwireDisarmed has not been raised. True while it waits.
	 */
	bool TrackWirePickup(FState& State, const AOperativeCharacter* Disarmer)
	{
		const ATripwireActor* Wire = State.Wire.Get();
		if (!Wire || !Wire->IsAwaitingPickup() || !Disarmer)
		{
			return false;
		}
		const int32 Events = State.Probe->CountEvent(GameplayEvents::TripwireDisarmed);
		if (!State.bAwaitSeen)
		{
			State.bAwaitSeen = true;
			State.AwaitAt = State.Time;
			State.AwaitDelay = Wire->GetLastPickupDelaySeconds();
			State.AwaitEvents = Events;
			State.AwaitGrenades = Disarmer->GrenadesCount;
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke wire disarmed, waiting %.2f s for the pickup grab"), State.AwaitDelay);
		}
		State.bAwaitWrong |= Events != State.AwaitEvents || Disarmer->GrenadesCount != State.AwaitGrenades || Wire->IsTripped();
		return true;
	}

	/** After the wire went: it waited for the grab (delay > 0.2 s) and only then the grenades / event came. */
	void CheckWirePickupOrder(FState& State, const TCHAR* Who)
	{
		const float Held = State.Time - State.AwaitAt;
		const bool bOk = State.bAwaitSeen && !State.bAwaitWrong && State.AwaitDelay > 0.2f && Held >= State.AwaitDelay - 0.15f
			&& State.Probe->CountEvent(GameplayEvents::TripwireDisarmed) == State.AwaitEvents + 1;
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s %s: the wire stayed %.2f s after the disarm (grab at %.2f s), the grenades / TripwireDisarmed after it"),
			bOk ? TEXT("ok  ") : TEXT("FAIL"), Who, State.bAwaitSeen ? Held : -1.f, State.AwaitDelay);
		State.Failures += bOk ? 0 : 1;
		State.bAwaitSeen = false;
		State.bAwaitWrong = false;
	}

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 ? TEXT("PASS") : TEXT("FAIL"));
		State.Probe.Reset();
		FPlatformMisc::RequestExit(false, TEXT("PMCTripwireSmoke"));
		return false;
	}

	void Place(AActor* Actor, const FVector& Ground)
	{
		if (AOperativeCharacter* Operative = Cast<AOperativeCharacter>(Actor))
		{
			Operative->StopOperative();
		}
		const ACharacter* Character = Cast<ACharacter>(Actor);
		Actor->SetActorLocation(Ground + FVector(0.f, 0.f, (Character ? Character->GetSimpleCollisionHalfHeight() : 50.f) + 2.f), false, nullptr,
			ETeleportType::TeleportPhysics);
	}

	/** A PMC wire as a level designer places it: actor at Centre, anchors 1.5 m either side along R (the viewport handles). */
	ATripwireActor* SpawnWire(UWorld* World, const FState& State, const FVector& Centre)
	{
		const FTransform Transform(State.R.Rotation(), Centre + FVector(0.f, 0.f, 5.f));
		APMCTripwireActor* Wire = World->SpawnActorDeferred<APMCTripwireActor>(APMCTripwireActor::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Wire)
		{
			Wire->AnchorA = FVector(-150.f, 0.f, 0.f);
			Wire->AnchorB = FVector(150.f, 0.f, 0.f);
			Wire->FinishSpawning(Transform);
		}
		return Wire;
	}

	/** Everybody 10-12 m behind the wire, standing. */
	void ParkSquad(USquadSubsystem* Squad, const FState& State)
	{
		int32 Index = 0;
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Member->SetStance(EOperativeStance::Standing);
			Place(Member, State.Centre - State.F * 1000.f + State.R * (Index++ * 200.f - 200.f));
		}
	}

	/** The path polyline from From to To with a filter class; empty when none. */
	TArray<FVector> PathPoints(UWorld* World, const FVector& From, const FVector& To, TSubclassOf<UNavigationQueryFilter> Filter)
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		const UNavigationPath* Path = Nav ? Nav->FindPathToLocationSynchronously(World, From, To, nullptr, Filter) : nullptr;
		return Path && Path->IsValid() ? Path->PathPoints : TArray<FVector>();
	}

	/** The path crosses the wire segment A-B (2D). */
	bool PathCrossesWire(const TArray<FVector>& Points, const FVector& A, const FVector& B)
	{
		for (int32 Index = 1; Index < Points.Num(); ++Index)
		{
			FVector Hit;
			if (FMath::SegmentIntersection2D(FVector(Points[Index - 1].X, Points[Index - 1].Y, 0.f), FVector(Points[Index].X, Points[Index].Y, 0.f),
				FVector(A.X, A.Y, 0.f), FVector(B.X, B.Y, 0.f), Hit))
			{
				return true;
			}
		}
		return false;
	}

	/** Closest 2D approach of the path to Point, cm. */
	float PathClearance(const TArray<FVector>& Points, const FVector& Point)
	{
		float Best = TNumericLimits<float>::Max();
		for (int32 Index = 1; Index < Points.Num(); ++Index)
		{
			const FVector P0(Points[Index - 1].X, Points[Index - 1].Y, 0.f);
			const FVector P1(Points[Index].X, Points[Index].Y, 0.f);
			Best = FMath::Min(Best, static_cast<float>(FMath::PointDistToSegment(FVector(Point.X, Point.Y, 0.f), P0, P1)));
		}
		return Best;
	}

	/** 2026-10-15: the disarmer stands within 0.9 m of the wire line, facing it (+-30 deg). */
	void CheckDisarmSpot(FState& State, const AOperativeCharacter* Operative, const ATripwireActor* Wire, const TCHAR* Pass)
	{
		if (!Operative || !Wire)
		{
			Check(State, false, FString::Printf(TEXT("%s disarm spot: no operative / wire"), Pass));
			return;
		}
		const FVector Focus = Wire->GetDisarmFocusPoint(Operative->GetActorLocation());
		const float Distance = static_cast<float>(FVector::Dist2D(Operative->GetActorLocation(), Focus));
		const FVector ToWire = (Focus - Operative->GetActorLocation()).GetSafeNormal2D();
		const float FacingDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Operative->GetActorForwardVector().GetSafeNormal2D(), ToWire), -1.0, 1.0)));
		Check(State, Distance <= 90.f && Distance >= 30.f && FacingDeg <= 30.f, FString::Printf(TEXT("%s: disarm spot %.0f cm from the wire line (<= 90, not on it), facing it (%.0f deg)"),
			Pass, Distance, FacingDeg));
	}

	bool IsClickable(const ATripwireActor* Wire)
	{
		return Wire && Wire->Box->GetCollisionResponseToChannel(ECC_Visibility) == ECR_Block && Wire->Mesh->IsVisible();
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += 0.1f;
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			const float* Cold = State.Cold.Find(Member);
			Member->ColdLevel = Cold ? *Cold : 0.f;
		}
		switch (State.Stage)
		{
		case 0:
		{
			if (State.Time < 3.f)
			{
				return true;
			}
			State.Probe.Reset(NewObject<UEventBusProbe>());
			State.Probe->ListenGameplayEvents(UCodexEventBus::Get(World));
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->HealthComponent->SetMaxHealth(100000.f);
				if (Member->SquadRole == EOperativeRole::Commander)
				{
					State.Commander = Member;
				}
				else if (Member->SquadRole == EOperativeRole::Engineer)
				{
					State.Engineer = Member;
				}
				else if (Member->SquadRole == EOperativeRole::MedicSapper)
				{
					State.Medic = Member;
				}
			}
			AOperativeCharacter* Commander = State.Commander.Get();
			if (!Commander || !State.Engineer.IsValid() || !State.Medic.IsValid() || (Flow && Flow->GetPhase() == ECodexGamePhase::WaveCombat))
			{
				Check(State, false, TEXT("exploration with commander, engineer and medic-sapper"));
				return Finish(State);
			}
			State.Ground = Commander->GetActorLocation() - FVector(0.f, 0.f, Commander->GetSimpleCollisionHalfHeight());
			State.F = Commander->GetActorForwardVector().GetSafeNormal2D();
			State.R = FVector::CrossProduct(FVector::UpVector, State.F);
			State.Centre = SmokeUtils::ClearPoint(World, State.Ground, State.Ground + State.F * 600.f);
			State.Centre.Z = State.Ground.Z;
			State.F = (State.Centre - State.Ground).GetSafeNormal2D();
			State.R = FVector::CrossProduct(FVector::UpVector, State.F);
			State.Second = SmokeUtils::ClearPoint(World, State.Centre, State.Centre + State.F * 400.f);
			State.Second.Z = State.Ground.Z;
			ParkSquad(Squad, State);
			ATripwireActor* Wire = SpawnWire(World, State, State.Centre);
			State.Wire = Wire;
			const float Span = Wire ? FVector::Dist2D(Wire->GetAnchorA(), Wire->GetAnchorB()) : 0.f;
			FOverheadLabel Label;
			Check(State, Wire && Wire->IsHostile() && Wire->IsArmed() && Wire->GetArmingLeft() <= 0.f && FMath::IsNearlyEqual(Span, 300.f, 5.f),
				FString::Printf(TEXT("placed PMC wire laid between its anchors at BeginPlay (%.0f cm), armed at once"), Span));
			Check(State, Wire && Wire->IsConcealed() && !IsClickable(Wire) && !Wire->GetOverheadLabel(Label),
				TEXT("hidden: not drawn, not clickable, no label"));
			State.Stage = 1;
			State.Time = 0.f;
			return true;
		}
		case 1:
		{
			if (State.Time < 1.f)
			{
				return true;
			}
			State.Hound = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::FrostHound, State.Centre + FVector(0.f, 0.f, 100.f));
			AEnemyCharacter* Hound = State.Hound.Get();
			if (!Hound)
			{
				Check(State, false, TEXT("hound spawned"));
				return Finish(State);
			}
			Hound->CustomTimeDilation = 0.f;
			Hound->GetHealthComponent()->SetMaxHealth(1000.f);
			State.HoundHealth = Hound->GetHealthComponent()->GetCurrentHealth();
			Place(Hound, State.Centre);
			State.Stage = 2;
			State.Time = 0.f;
			return true;
		}
		case 2:
		{
			if (State.Time < 1.f)
			{
				return true;
			}
			ATripwireActor* Wire = State.Wire.Get();
			AEnemyCharacter* Hound = State.Hound.Get();
			Check(State, Wire && Wire->IsArmed() && Wire->IsConcealed() && Hound && Hound->GetHealthComponent()->GetCurrentHealth() >= State.HoundHealth,
				TEXT("an enemy on the PMC wire does not set it off"));
			if (Hound)
			{
				Hound->Destroy();
			}
			State.Engineer->SetStance(EOperativeStance::Crouching);
			Place(State.Engineer.Get(), State.Centre - State.F * 550.f);
			State.Stage = 3;
			State.Time = 0.f;
			return true;
		}
		case 3:
		{
			if (State.Time < 1.f)
			{
				return true;
			}
			ATripwireActor* Wire = State.Wire.Get();
			Check(State, Wire && Wire->IsConcealed(), TEXT("the crouched engineer at 5.5 m does not spot it (4.5 m)"));
			ParkSquad(Squad, State);
			State.Medic->SetStance(EOperativeStance::Crouching);
			Place(State.Medic.Get(), State.Centre - State.F * 550.f);
			State.Stage = 4;
			State.Time = 0.f;
			return true;
		}
		case 4:
		{
			ATripwireActor* Wire = State.Wire.Get();
			if (Wire && Wire->IsConcealed() && State.Time < 2.f)
			{
				return true;
			}
			Check(State, Wire && Wire->IsSpotted() && IsClickable(Wire) && Wire->IsArmed(),
				FString::Printf(TEXT("the crouched Medic-Sapper at 5.5 m spots it (6 m) in %.1f s: drawn and clickable"), State.Time));
			Check(State, State.Probe->CountEvent(GameplayEvents::TripwireSpotted, State.Medic->GetFName()) == 1,
				TEXT("TripwireSpotted raised once with the medic-sapper as the subject"));
			// The Commander, frozen hands, crouched next to the wire.
			ParkSquad(Squad, State);
			AOperativeCharacter* Commander = State.Commander.Get();
			State.Cold.Add(Commander, 80.f);
			Commander->ColdLevel = 80.f;
			Place(Commander, State.Centre - State.F * 120.f);
			const FDefusalChance Warm = TripwireRules::GetHostileDisarmChance(EOperativeRole::Commander, Commander->Luck, 0.f, 0, false);
			const FDefusalChance Odds = Wire ? Wire->GetDefusalChance(Commander) : FDefusalChance();
			Check(State, FMath::IsNearlyEqual(Odds.ColdPenalty, 50.f) && FMath::IsNearlyEqual(Odds.Chance, FMath::Max(1.f, Warm.Chance - 50.f), 0.01f) && Odds.bDangerous,
				FString::Printf(TEXT("commander at 80 %% cold: %.1f %% (warm %.1f %%, -%.0f), dangerous"), Odds.Chance, Warm.Chance, Odds.ColdPenalty));
			Check(State, Wire && FMath::IsNearlyEqual(Wire->GetDisarmSecondsFor(Commander), 5.f) && FMath::IsNearlyEqual(Wire->GetDisarmSecondsFor(State.Medic.Get()), 3.f)
				&& Wire->CanBeDisarmedBy(Commander), TEXT("anyone may disarm: commander 5 s, medic-sapper 3 s"));
			if (Wire)
			{
				Wire->DefusalRollForTesting = 99.f; // a miss ...
				Wire->DefusalExplosionRollForTesting = 0.f; // ... that sets it off
				Wire->ExecuteAction(Commander);
			}
			State.CommanderHealth = Commander->HealthComponent->GetCurrentHealth();
			State.Stage = 5;
			State.Time = 0.f;
			return true;
		}
		case 5:
		{
			if (State.Time < 4.5f)
			{
				ATripwireActor* Wire = State.Wire.Get();
				if (State.Time > 1.f && !State.bCheckedWorking)
				{
					State.bCheckedWorking = true;
					Check(State, Wire && Wire->IsDisarming(), TEXT("the commander is still working at 1 s"));
				}
				return true;
			}
			if (State.Time < 5.4f)
			{
				return true;
			}
			ATripwireActor* Wire = State.Wire.Get();
			Check(State, Wire && Wire->IsArmed() && !Wire->IsTripped() && Wire->bDefusalWarned && Wire->FailedDefusalAttempts == 0,
				TEXT("first frozen attempt (5 s): only the warning (mine rule), the wire stays armed"));
			if (Wire)
			{
				Wire->ExecuteAction(State.Commander.Get());
			}
			State.Stage = 6;
			State.Time = 0.f;
			return true;
		}
		case 6:
		{
			if (State.Time < 6.f)
			{
				return true;
			}
			AOperativeCharacter* Commander = State.Commander.Get();
			const float Lost = State.CommanderHealth - Commander->HealthComponent->GetCurrentHealth();
			Check(State, !State.Wire.IsValid() && Lost > 0.f && State.Probe->CountEvent(GameplayEvents::TripwireTriggered) == 1,
				FString::Printf(TEXT("second attempt fails: the pin is pulled, blast (commander -%.0f HP), wire gone, TripwireTriggered"), Lost));
			State.Cold.Remove(Commander);
			ParkSquad(Squad, State);
			ATripwireActor* Wire = SpawnWire(World, State, State.Centre);
			State.Wire = Wire;
			if (Wire)
			{
				Wire->Reveal();
				Wire->DefusalRollForTesting = 0.f;
				Wire->DefusalExplosionRollForTesting = 99.f;
			}
			Place(State.Medic.Get(), State.Centre - State.F * 120.f);
			State.MedicGrenades = State.Medic->GrenadesCount;
			State.Stage = 7;
			State.Time = 0.f;
			return true;
		}
		case 7:
		{
			if (State.Time < 1.f)
			{
				return true;
			}
			if (ATripwireActor* Wire = State.Wire.Get())
			{
				Wire->ExecuteAction(State.Medic.Get());
			}
			State.Stage = 8;
			State.Time = 0.f;
			return true;
		}
		case 8:
		{
			if (State.Time < 3.5f)
			{
				return true;
			}
			Check(State, !State.Wire.IsValid() && State.Medic->GrenadesCount == State.MedicGrenades + 2
				&& State.Probe->CountEvent(GameplayEvents::TripwireDisarmed, State.Medic->GetFName()) == 1,
				FString::Printf(TEXT("the medic-sapper disarms a PMC wire in 3 s: removed, +%d grenades, TripwireDisarmed"), State.Medic->GrenadesCount - State.MedicGrenades));
			ParkSquad(Squad, State);
			ATripwireActor* Wire = SpawnWire(World, State, State.Centre);
			State.Wire = Wire;
			if (Wire)
			{
				Wire->Reveal();
			}
			State.Engineer->SetStance(EOperativeStance::Prone);
			State.Stage = 9;
			State.Time = 0.f;
			return true;
		}
		case 9:
			if (State.Time < 2.f)
			{
				return true;
			}
			Place(State.Engineer.Get(), State.Centre);
			State.Stage = 10;
			State.Time = 0.f;
			return true;
		case 10:
		{
			if (State.Time < 1.f)
			{
				return true;
			}
			ATripwireActor* Wire = State.Wire.Get();
			Check(State, Wire && Wire->IsArmed(), TEXT("the engineer lying prone on the PMC wire crawls under it"));
			State.Engineer->SetStance(EOperativeStance::Standing);
			Place(State.Engineer.Get(), State.Centre);
			State.Stage = 11;
			State.Time = 0.f;
			return true;
		}
		case 11:
		{
			if (State.Time < 1.5f)
			{
				return true;
			}
			Check(State, !State.Wire.IsValid() && State.Probe->CountEvent(GameplayEvents::TripwireTriggered, State.Engineer->GetFName()) == 1,
				TEXT("standing up on the wire trips it (TripwireTriggered by the engineer)"));
			// Rev 5e: known-trap pathing - a spotted wire across the way, the squad AUTO mode off.
			ParkSquad(Squad, State);
			Squad->SetAutonomousSquadCombat(false);
			ATripwireActor* Wire = SpawnWire(World, State, State.Centre);
			State.Wire = Wire;
			if (Wire)
			{
				Wire->Reveal();
			}
			State.Stage = 20;
			State.Time = 0.f;
			return true;
		}
		case 20:
		{
			if (State.Time < 2.f)
			{
				return true; // the known-trap area is in the navmesh
			}
			ATripwireActor* Wire = State.Wire.Get();
			const FVector From = State.Centre - State.F * 350.f + FVector(0.f, 0.f, 90.f);
			const FVector To = State.Centre + State.F * 350.f + FVector(0.f, 0.f, 90.f);
			const TArray<FVector> Literal = PathPoints(World, From, To, UNavFilter_NoCrawlTrapLiteral::StaticClass());
			const TArray<FVector> Avoid = PathPoints(World, From, To, UNavFilter_NoCrawl::StaticClass());
			Check(State, Wire && Literal.Num() >= 2 && PathCrossesWire(Literal, Wire->GetAnchorA(), Wire->GetAnchorB()),
				FString::Printf(TEXT("player-control filter: the path runs straight through the spotted wire (%d points)"), Literal.Num()));
			Check(State, Wire && Avoid.Num() >= 2 && !PathCrossesWire(Avoid, Wire->GetAnchorA(), Wire->GetAnchorB()),
				FString::Printf(TEXT("AUTO filter: the path goes round the spotted wire (%d points)"), Avoid.Num()));
			// A literal order: the commander, AUTO off, sent through the spotted wire.
			AOperativeCharacter* Commander = State.Commander.Get();
			Place(Commander, State.Centre - State.F * 350.f);
			Commander->SetStance(EOperativeStance::Standing);
			Squad->SetLeader(Commander);
			Check(State, !Squad->IsAutonomousSquadCombat() && Commander->OrderMoveTo(State.Centre + State.F * 350.f, false) == EOperativeOrderResult::Accepted,
				TEXT("AUTO off: the commander is ordered across the spotted wire"));
			State.Stage = 21;
			State.Time = 0.f;
			return true;
		}
		case 21:
		{
			if (State.Wire.IsValid() && State.Time < 8.f)
			{
				return true;
			}
			Check(State, !State.Wire.IsValid() && State.Probe->CountEvent(GameplayEvents::TripwireTriggered, State.Commander->GetFName()) == 1,
				FString::Printf(TEXT("player control: the order is literal - he walks into the spotted wire and trips it (%.1f s)"), State.Time));
			ParkSquad(Squad, State);
			ATripwireActor* Wire = SpawnWire(World, State, State.Centre);
			State.Wire = Wire;
			if (Wire)
			{
				Wire->Reveal();
			}
			Squad->SetAutonomousSquadCombat(true); // Ctrl+T
			State.Stage = 22;
			State.Time = 0.f;
			return true;
		}
		case 22:
		{
			if (State.Time < 2.f)
			{
				return true;
			}
			AOperativeCharacter* Commander = State.Commander.Get();
			Place(Commander, State.Centre - State.F * 350.f);
			Check(State, Squad->IsAutonomousSquadCombat() && Commander->OrderMoveTo(State.Centre + State.F * 350.f, false) == EOperativeOrderResult::Accepted,
				TEXT("AUTO on (Ctrl+T): the same order across the spotted wire"));
			State.Stage = 23;
			State.Time = 0.f;
			return true;
		}
		case 23:
		{
			AOperativeCharacter* Commander = State.Commander.Get();
			const float Left = static_cast<float>(FVector::Dist2D(Commander->GetActorLocation(), State.Centre + State.F * 350.f));
			if (Left > 120.f && State.Wire.IsValid() && State.Time < 12.f)
			{
				return true;
			}
			Check(State, State.Wire.IsValid() && State.Wire->IsArmed() && Left <= 120.f
				&& State.Probe->CountEvent(GameplayEvents::TripwireTriggered, Commander->GetFName()) == 1,
				FString::Printf(TEXT("AUTO mode: he goes round the spotted wire and arrives (%.0f cm left, %.1f s), no trip"), Left, State.Time));
			Squad->SetAutonomousSquadCombat(false);
			if (ATripwireActor* Wire = State.Wire.Get())
			{
				Wire->Destroy();
			}
			ParkSquad(Squad, State);
			// A spotted mine: the same known-trap area (rev 5e; mines had no avoidance before).
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AProximityMineActor* Mine = World->SpawnActor<AProximityMineActor>(State.Centre + FVector(0.f, 0.f, 30.f), FRotator::ZeroRotator, Params);
			State.Mine = Mine;
			Check(State, Mine && !Mine->IsRevealed() && !Mine->HasKnownTrapNav(), TEXT("a hidden mine has no path cost"));
			if (Mine)
			{
				Mine->Reveal();
			}
			State.Stage = 24;
			State.Time = 0.f;
			return true;
		}
		case 24:
		{
			if (State.Time < 2.f)
			{
				return true;
			}
			AProximityMineActor* Mine = State.Mine.Get();
			const FVector From = State.Centre - State.F * 400.f + FVector(0.f, 0.f, 90.f);
			const FVector To = State.Centre + State.F * 400.f + FVector(0.f, 0.f, 90.f);
			const TArray<FVector> Literal = PathPoints(World, From, To, UNavFilter_NoCrawlTrapLiteral::StaticClass());
			const TArray<FVector> Avoid = PathPoints(World, From, To, UNavFilter_NoCrawl::StaticClass());
			const float LiteralClear = PathClearance(Literal, State.Centre);
			const float AvoidClear = PathClearance(Avoid, State.Centre);
			Check(State, Mine && Mine->HasKnownTrapNav() && Literal.Num() >= 2 && Avoid.Num() >= 2 && LiteralClear < 60.f && AvoidClear > 140.f,
				FString::Printf(TEXT("a spotted mine: known-trap area; the literal path passes %.0f cm from it, the AUTO path %.0f cm"), LiteralClear, AvoidClear));
			if (Mine)
			{
				Mine->Destroy();
			}
			// Rev 5e: the real click path - a spotted wire, the engineer (leader, warm) clicks the ground right beside it.
			ParkSquad(Squad, State);
			ATripwireActor* Wire = SpawnWire(World, State, State.Centre);
			State.Wire = Wire;
			if (Wire)
			{
				Wire->Reveal();
				Wire->DefusalRollForTesting = 0.f; // a good roll (the odds are HostileDisarmRoleCold's job)
				Wire->DefusalExplosionRollForTesting = 99.f;
			}
			AOperativeCharacter* Engineer = State.Engineer.Get();
			Engineer->SetStance(EOperativeStance::Standing);
			Place(Engineer, State.Centre - State.F * 350.f);
			Squad->SetLeader(Engineer);
			State.MedicGrenades = Engineer->GrenadesCount;
			State.Stage = 26;
			State.Time = 0.f;
			return true;
		}
		case 26:
		{
			if (State.Time < 1.f)
			{
				return true;
			}
			ATripwireActor* Wire = State.Wire.Get();
			ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(World->GetFirstPlayerController());
			if (!Wire || !PC)
			{
				Check(State, false, TEXT("wire / controller"));
				return Finish(State);
			}
			FOverheadLabel Label;
			Check(State, Wire->GetOverheadLabel(Label) && Label.Text.Contains(TEXT("click to disarm")),
				FString::Printf(TEXT("the spotted wire's label says how: '%s'"), *Label.Text));
			// The ground 25 cm in front of the wire (on the engineer's side), no actor hit - what a slightly-off cursor gives.
			const FVector Ground = State.Centre - State.F * 25.f + State.R * 40.f;
			Check(State, ATripwireActor::FindClickableWireNear(World, Ground) == Wire && !ATripwireActor::FindClickableWireNear(World, State.Centre - State.F * 120.f),
				TEXT("the ground 25 cm beside the wire counts as the wire (hand cursor), 1.2 m away does not"));
			FHitResult GroundHit;
			GroundHit.ImpactPoint = Ground;
			GroundHit.Location = Ground;
			GroundHit.ImpactNormal = FVector::UpVector;
			GroundHit.Normal = FVector::UpVector;
			GroundHit.bBlockingHit = true;
			PC->HandleWorldHit(GroundHit);
			State.Stage = 27;
			State.Time = 0.f;
			return true;
		}
		case 27:
		{
			UInteractionSubsystem* Interactions = World->GetSubsystem<UInteractionSubsystem>();
			ATripwireActor* Wire = State.Wire.Get();
			if (Wire && Interactions && !Interactions->IsActionMenuOpen() && State.Time < 10.f)
			{
				return true;
			}
			Check(State, Wire && Interactions && Interactions->IsActionMenuOpen() && Interactions->GetMenuTarget() == Wire && Wire->IsArmed(),
				FString::Printf(TEXT("the click walked the engineer up to the wire (not into it) and opened its menu (%.1f s)"), State.Time));
			CheckDisarmSpot(State, State.Engineer.Get(), Wire, TEXT("standing"));
			if (Interactions && Interactions->IsActionMenuOpen())
			{
				Interactions->ConfirmActionMenu(); // «Disarm (4 s)»
			}
			Check(State, Wire && Wire->IsDisarming(), TEXT("Disarm confirmed: he works on it"));
			State.Stage = 28;
			State.Time = 0.f;
			return true;
		}
		case 28:
		{
			TrackWirePickup(State, State.Engineer.Get());
			if (State.Wire.IsValid() && State.Time < 7.f)
			{
				return true;
			}
			CheckWirePickupOrder(State, TEXT("standing"));
			AOperativeCharacter* Engineer = State.Engineer.Get();
			Check(State, !State.Wire.IsValid() && State.Probe->CountEvent(GameplayEvents::TripwireDisarmed, Engineer->GetFName()) == 1
				&& Engineer->GrenadesCount == State.MedicGrenades + 2 && State.Probe->CountEvent(GameplayEvents::TripwireTriggered, Engineer->GetFName()) == 1,
				FString::Printf(TEXT("the real input disarms the PMC wire: gone after %.1f s, TripwireDisarmed by the engineer, +%d grenades, no new trip"), State.Time,
					Engineer->GrenadesCount - State.MedicGrenades));
			// 2026-10-15: standing before the work -> stood up again, the full-body pickup (rising from the low pose).
			const UOperativeAnimInstance* Anim = UOperativeAnimInstance::Of(Engineer);
			Check(State, Engineer->GetStance() == EOperativeStance::Standing && Anim && Anim->GetInteractionClipLog().Contains(TEXT("PickupGround@FullBody")),
				FString::Printf(TEXT("standing disarmer: stands up, pickup clip on the FULL body (stance %d, clips %s)"), static_cast<int32>(Engineer->GetStance()),
					Anim ? *FString::Join(Anim->GetInteractionClipLog(), TEXT(", ")) : TEXT("-")));
			// Next: the same with a crouched disarmer (upper-body pickup, legs stay crouched).
			ParkSquad(Squad, State);
			ATripwireActor* Next = SpawnWire(World, State, State.Centre);
			State.Wire = Next;
			if (Next)
			{
				Next->Reveal();
				Next->DefusalRollForTesting = 0.f;
				Next->DefusalExplosionRollForTesting = 99.f;
			}
			Place(Engineer, State.Centre - State.F * 350.f);
			Engineer->SetStance(EOperativeStance::Crouching);
			Squad->SetLeader(Engineer);
			State.Stage = 29;
			State.Time = 0.f;
			return true;
		}
		case 29:
		{
			UInteractionSubsystem* Interactions = World->GetSubsystem<UInteractionSubsystem>();
			ATripwireActor* Wire = State.Wire.Get();
			if (State.Time < 1.f || !Wire || !Interactions)
			{
				if (State.Time >= 1.f)
				{
					Check(State, false, TEXT("crouched pass: wire"));
					return Finish(State);
				}
				return true;
			}
			if (!State.bRequested)
			{
				State.bRequested = true;
				Interactions->RequestInteraction(Wire); // the click on the wire itself
				return true;
			}
			if (!Interactions->IsActionMenuOpen() && State.Time < 12.f)
			{
				return true;
			}
			Check(State, Interactions->IsActionMenuOpen() && Interactions->GetMenuTarget() == Wire,
				FString::Printf(TEXT("crouched: walked up and the Disarm menu opened (%.1f s)"), State.Time));
			CheckDisarmSpot(State, State.Engineer.Get(), Wire, TEXT("crouched"));
			if (Interactions->IsActionMenuOpen())
			{
				Interactions->ConfirmActionMenu();
			}
			State.Stage = 30;
			State.Time = 0.f;
			return true;
		}
		case 30:
		{
			TrackWirePickup(State, State.Engineer.Get());
			if (State.Wire.IsValid() && State.Time < 7.f)
			{
				return true;
			}
			CheckWirePickupOrder(State, TEXT("crouched"));
			AOperativeCharacter* Engineer = State.Engineer.Get();
			const UOperativeAnimInstance* Anim = UOperativeAnimInstance::Of(Engineer);
			Check(State, !State.Wire.IsValid() && Engineer->GetStance() == EOperativeStance::Crouching && Anim
				&& Anim->GetInteractionClipLog().Contains(TEXT("PickupGround@UpperBody")) && Anim->InteractionLayer == EInteractionAnimLayer::UpperBody,
				FString::Printf(TEXT("crouched disarmer: stays crouched, pickup clip on the UPPER body (clips %s)"),
					Anim ? *FString::Join(Anim->GetInteractionClipLog(), TEXT(", ")) : TEXT("-")));
			State.PickupsBeforeProne = Anim ? Anim->GetInteractionClipLog().Num() : 0;
			// Prone: a third wire disarmed by the engineer lying next to it (the squad's prone rule: no clip yet).
			ParkSquad(Squad, State);
			ATripwireActor* Next = SpawnWire(World, State, State.Centre);
			State.Wire = Next;
			if (Next)
			{
				Next->Reveal();
				Next->DefusalRollForTesting = 0.f;
				Next->DefusalExplosionRollForTesting = 99.f;
			}
			Engineer->SetStance(EOperativeStance::Prone);
			Place(Engineer, State.Centre - State.F * 70.f);
			State.Stage = 31;
			State.Time = 0.f;
			return true;
		}
		case 31:
		{
			if (State.Time < 2.f)
			{
				return true;
			}
			if (ATripwireActor* Wire = State.Wire.Get(); Wire && !Wire->IsDisarming())
			{
				Wire->ExecuteAction(State.Engineer.Get()); // the PMC wire work stance is crouched: he came in prone
			}
			State.Stage = 32;
			State.Time = 0.f;
			return true;
		}
		case 32:
		{
			if (State.Wire.IsValid() && State.Time < 7.f)
			{
				return true;
			}
			const UOperativeAnimInstance* Anim = UOperativeAnimInstance::Of(State.Engineer.Get());
			Check(State, !State.Wire.IsValid() && Anim && Anim->GetInteractionClipLog().Num() == State.PickupsBeforeProne,
				FString::Printf(TEXT("prone disarmer: disarmed, NO pickup clip (prone clip later; %d clips before, %d after)"), State.PickupsBeforeProne,
					Anim ? Anim->GetInteractionClipLog().Num() : -1));
			State.Engineer->SetStance(EOperativeStance::Standing);
			State.Stage = 25;
			State.Time = 0.f;
			return true;
		}
		case 25:
		{
			if (State.Time < 0.5f)
			{
				return true;
			}
			// Save / load: one hidden, one spotted wire.
			ParkSquad(Squad, State);
			ATripwireActor* HiddenWire = SpawnWire(World, State, State.Centre);
			ATripwireActor* SpottedWire = SpawnWire(World, State, State.Second);
			if (!HiddenWire || !SpottedWire)
			{
				Check(State, false, TEXT("two wires for the save"));
				return Finish(State);
			}
			SpottedWire->Reveal();
			State.Hidden = HiddenWire;
			State.Spotted = SpottedWire;
			State.HiddenName = HiddenWire->GetName();
			State.SpottedName = SpottedWire->GetName();
			State.Stage = 12;
			State.Time = 0.f;
			return true;
		}
		case 12:
		{
			if (State.Time < 1.f)
			{
				return true;
			}
			USaveGameSubsystem* Saves = World->GetSubsystem<USaveGameSubsystem>();
			Saves->SaveDirectoryOverride = FPaths::ProjectSavedDir() / TEXT("SmokeSaves") / TEXT("PMCTripwire");
			IFileManager::Get().DeleteDirectory(*Saves->SaveDirectoryOverride, false, true);
			const bool bSaved = Saves->SaveGame(TEXT("pmc_wires"));
			Check(State, bSaved, TEXT("saved with a hidden and a spotted PMC wire"));
			if (ATripwireActor* HiddenWire = State.Hidden.Get())
			{
				HiddenWire->Reveal();
			}
			if (ATripwireActor* SpottedWire = State.Spotted.Get())
			{
				SpottedWire->Destroy();
			}
			State.Stage = 13;
			State.Time = 0.f;
			return true;
		}
		case 13:
		{
			if (State.Time < 0.5f)
			{
				return true;
			}
			USaveGameSubsystem* Saves = World->GetSubsystem<USaveGameSubsystem>();
			Check(State, Saves->LoadGame(TEXT("pmc_wires")), TEXT("loaded"));
			State.Stage = 14;
			State.Time = 0.f;
			return true;
		}
		default:
		{
			if (State.Time < 1.f)
			{
				return true;
			}
			ATripwireActor* HiddenWire = nullptr;
			ATripwireActor* SpottedWire = nullptr;
			int32 Count = 0;
			for (TActorIterator<APMCTripwireActor> It(World); It; ++It)
			{
				if (It->IsActorBeingDestroyed())
				{
					continue;
				}
				++Count;
				HiddenWire = It->GetName() == State.HiddenName ? *It : HiddenWire;
				// A wire recreated by the load gets a new name when the old one is still held by the destroyed actor: match by place.
				SpottedWire = It->GetName() != State.HiddenName && FVector::Dist2D(It->GetActorLocation(), State.Second) < 30.f ? *It : SpottedWire;
			}
			Check(State, Count == 2 && HiddenWire && SpottedWire, FString::Printf(TEXT("load: both saved PMC wires present (%d)"), Count));
			Check(State, HiddenWire && HiddenWire->IsHostile() && HiddenWire->IsConcealed() && HiddenWire->IsArmed() && !IsClickable(HiddenWire),
				TEXT("load: the hidden wire is hidden again (revealed after the save)"));
			Check(State, SpottedWire && SpottedWire->IsHostile() && SpottedWire->IsSpotted() && SpottedWire->IsArmed() && IsClickable(SpottedWire)
				&& FVector::Dist2D(SpottedWire->GetActorLocation(), State.Second) < 30.f
				&& FMath::IsNearlyEqual(FVector::Dist2D(SpottedWire->GetAnchorA(), SpottedWire->GetAnchorB()), 300.f, 5.f),
				TEXT("load: the removed spotted wire is back, spotted and armed between its anchors"));
			return Finish(State);
		}
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), 0.1f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.PMCTripwireSmoke"),
		TEXT("Dev check of the Demo 01 PMC tripwire: placed hidden + armed, enemies pass, spotting (sapper bonus), cold commander disarm fails, sapper disarms, prone passes, save / load."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
