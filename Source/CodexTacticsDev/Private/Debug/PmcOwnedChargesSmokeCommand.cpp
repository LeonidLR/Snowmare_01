// Dev-only check of the PMC-owned charges (smart-enemies T-11 second half, PMC_DEFENCE D2, designer OQ-21) on the Depot:
//   Scripts/smoke_parallel.ps1 -Commands PmcOwnedChargesSmoke   (map /Game/Maps/Generated/L_Gen_Depot_1, "-LevelJson=L_Gen_Depot_1.json")
// No map / level JSON edit: the smoke spawns mines and PMC tripwires on the navmesh and marks them with PmcEncounterNav::AdoptCharge, the
// very call the props spawner makes for the pmc_charges layer (owner "pmc"). It checks that
//   - a PMC fighter standing on an ordinary level mine / wire sets it off (control: today's rule), on an OWNED one he does not,
//   - the player's man on an owned mine / wire still sets it off (the player's rules are unchanged),
//   - a navmesh path of the PMC fighters' filter (PmcEncounterNav::FilterFor) goes round an owned mine while the default (player) filter
//     goes straight over it, also once the squad has spotted it (the known-trap area is added over the PMC one).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/PmcEncounterNav.h"
#include "AI/PmcEncounterRules.h"
#include "AI/PmcEncounterSubsystem.h"
#include "AI/PmcSquadSubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/CombatPressureSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/ProximityMineActor.h"
#include "Interactables/TripwireActor.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"

namespace PmcOwnedChargesSmoke
{
	constexpr float StepSeconds = 0.1f;
	constexpr double GiveUpSeconds = 240.0;

	struct FState
	{
		int32 Stage = 0;
		double StageStart = -1.0;
		int32 Failures = 0;
		TWeakObjectPtr<AOperativeCharacter> Fighter;
		TWeakObjectPtr<AProximityMineActor> ControlMine;
		TWeakObjectPtr<AProximityMineActor> OwnedMine;
		TWeakObjectPtr<AProximityMineActor> PathMine;
		TWeakObjectPtr<ATripwireActor> ControlWire;
		TWeakObjectPtr<ATripwireActor> OwnedWire;
		FVector Open = FVector::ZeroVector;
		FPmcEncounterSpec FighterSpec;
		int32 FighterSeed = 5;
		FVector PathFrom = FVector::ZeroVector;
		FVector PathTo = FVector::ZeroVector;
		FVector PathCharge = FVector::ZeroVector;
		float MineRadius = 0.f;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("PmcOwnedChargesSmoke"));
		return false;
	}

	bool NavPointAt(UNavigationSystemV1& Nav, const FVector& Centre, float DistanceCm, float BearingDeg, float GroundZ, FVector& Out)
	{
		const float Angle = FMath::DegreesToRadians(BearingDeg);
		const FVector Want = Centre + FVector(FMath::Cos(Angle) * DistanceCm, FMath::Sin(Angle) * DistanceCm, 0.f);
		FNavLocation OnNav;
		if (!Nav.ProjectPointToNavigation(Want, OnNav, FVector(200.f, 200.f, 300.f)) || FMath::Abs(OnNav.Location.Z - GroundZ) > 120.f)
		{
			return false;
		}
		Out = OnNav.Location;
		return true;
	}

	AProximityMineActor* SpawnMine(UWorld& World, const FVector& Ground, bool bOwned)
	{
		AProximityMineActor* Mine = World.SpawnActorDeferred<AProximityMineActor>(AProximityMineActor::StaticClass(), FTransform(Ground + FVector(0.f, 0.f, 20.f)), nullptr,
			nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Mine)
		{
			return nullptr;
		}
		Mine->bPlacedBySquad = false;
		Mine->FinishSpawning(FTransform(Ground + FVector(0.f, 0.f, 20.f)));
		Mine->RestoreSaved(/*bInPlacedBySquad=*/ false, /*bInTrapped=*/ true, /*bInRevealed=*/ false, /*InArmingTimeLeft=*/ 0.f);
		if (bOwned)
		{
			const float Half = Mine->TriggerRadius + PmcEncounterNav::OwnChargeMarginCm;
			PmcEncounterNav::AdoptCharge(*Mine, FVector(Half, Half, 60.f));
		}
		return Mine;
	}

	ATripwireActor* SpawnWire(UWorld& World, const FVector& Ground, bool bOwned)
	{
		const FTransform Transform(Ground + FVector(0.f, 0.f, 30.f));
		ATripwireActor* Wire = World.SpawnActorDeferred<ATripwireActor>(ATripwireActor::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Wire)
		{
			return nullptr;
		}
		Wire->Faction = ETripwireFaction::PMC;
		Wire->bArmedAtLevelStart = true;
		Wire->AnchorA = FVector(0.f, -150.f, 0.f);
		Wire->AnchorB = FVector(0.f, 150.f, 0.f);
		Wire->FinishSpawning(Transform);
		if (bOwned)
		{
			PmcEncounterNav::AdoptCharge(*Wire, FVector(210.f, 60.f, 40.f));
		}
		return Wire;
	}

	/** The living fighter: the one from before, else a fresh squad of one on the open spot (a blast killed the last). */
	AOperativeCharacter* EnsureFighter(FState& State, UPmcEncounterSubsystem& Encounters, UPmcSquadSubsystem& Pmc)
	{
		AOperativeCharacter* Fighter = State.Fighter.Get();
		if (Fighter && !Fighter->IsKilledInAction() && Fighter->HealthComponent && Fighter->HealthComponent->IsAlive())
		{
			return Fighter;
		}
		const int32 TeamId = Encounters.SpawnEncounter(State.FighterSpec, ++State.FighterSeed);
		const TArray<AOperativeCharacter*> Men = TeamId != INDEX_NONE ? Pmc.GetTeamMembers(TeamId) : TArray<AOperativeCharacter*>();
		State.Fighter = Men.IsEmpty() ? nullptr : Men[0];
		return State.Fighter.Get();
	}

	/** The smallest planar distance of the path's polyline to Point. */
	float PathMissCm(const TArray<FVector>& Points, const FVector& Point)
	{
		float Best = TNumericLimits<float>::Max();
		for (int32 Index = 0; Index + 1 < Points.Num(); ++Index)
		{
			Best = FMath::Min(Best, static_cast<float>(FMath::PointDistToSegment(FVector(Point.X, Point.Y, 0.0), FVector(Points[Index].X, Points[Index].Y, 0.0),
				FVector(Points[Index + 1].X, Points[Index + 1].Y, 0.0))));
		}
		return Best;
	}

	bool PathPoints(UWorld& World, UNavigationSystemV1& Nav, const FVector& From, const FVector& To, TSubclassOf<UNavigationQueryFilter> Filter, TArray<FVector>& Out)
	{
		const UNavigationPath* Path = Nav.FindPathToLocationSynchronously(&World, From, To, nullptr, Filter);
		if (!Path || !Path->IsValid() || Path->IsPartial())
		{
			return false;
		}
		Out = Path->PathPoints;
		return Out.Num() >= 2;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, TSharedRef<FState> StateRef)
	{
		FState& State = *StateRef;
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		const double Now = World->GetTimeSeconds();
		if (State.StageStart < 0.0)
		{
			State.StageStart = Now;
		}
		const double InStage = Now - State.StageStart;
		auto Next = [&State, Now](int32 Stage)
		{
			State.Stage = Stage;
			State.StageStart = Now;
		};
		if (Now > GiveUpSeconds)
		{
			Check(State, false, FString::Printf(TEXT("finished within %.0f s of game time (stage %d)"), GiveUpSeconds, State.Stage));
			return Finish(State, false);
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World);
		UPmcEncounterSubsystem* Encounters = UPmcEncounterSubsystem::Get(World);
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		if (!Squad || !Pmc || !Encounters || !Nav)
		{
			Check(State, false, TEXT("squad / PMC / encounter subsystems and navigation present"));
			return Finish(State, false);
		}
		AOperativeCharacter* Mate = Squad->GetMembers().IsEmpty() ? nullptr : Squad->GetMembers()[0];
		switch (State.Stage)
		{
		case 0:
		{
			if (InStage < 2.0 || !Mate)
			{
				return true;
			}
			Encounters->CancelPending();
			if (IConsoleVariable* Immortal = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Cheat.Immortal")))
			{
				Immortal->Set(1, ECVF_SetByCode);
			}
			if (UCombatPressureSubsystem* Pressure = World->GetSubsystem<UCombatPressureSubsystem>())
			{
				Pressure->SetAutoSpawn(false);
				Pressure->DespawnAll();
			}
			const float GroundZ = static_cast<float>(Mate->GetActorLocation().Z) - 92.f;
			const FVector Centre = Mate->GetActorLocation();
			// Three open spots: the fighter's place, the control charges', the owned charges'.
			TArray<FVector> Spots;
			for (const float DistanceCm : { 3000.f, 4000.f, 2500.f, 5000.f })
			{
				for (int32 Bearing = 0; Bearing < 360 && Spots.Num() < 3; Bearing += 20)
				{
					FVector Point;
					if (NavPointAt(*Nav, Centre, DistanceCm, static_cast<float>(Bearing), GroundZ, Point)
						&& !Spots.ContainsByPredicate([&Point](const FVector& Taken) { return FVector::Dist2D(Taken, Point) < 1500.f; }))
					{
						Spots.Add(Point);
					}
				}
				if (Spots.Num() >= 3)
				{
					break;
				}
			}
			Check(State, Spots.Num() >= 3, FString::Printf(TEXT("three open navmesh spots found (%d)"), Spots.Num()));
			if (Spots.Num() < 3)
			{
				return Finish(State, false);
			}
			// One PMC fighter on a post spot (T-11 first half), immortal.
			FPmcEncounterSpec Spec;
			Spec.Count = 1;
			Spec.Posture = EPmcPosture::Defend;
			Spec.bHasPost = true;
			Spec.bRandomSpawn = false;
			Spec.Post.Pos = Spots[0];
			Spec.DelaySeconds = 0.f;
			State.FighterSpec = Spec;
			FString Why;
			const int32 TeamId = Encounters->SpawnEncounter(Spec, 5, &Why);
			const TArray<AOperativeCharacter*> Men = TeamId != INDEX_NONE ? Pmc->GetTeamMembers(TeamId) : TArray<AOperativeCharacter*>();
			Check(State, Men.Num() == 1, FString::Printf(TEXT("a PMC fighter spawned (%s)"), *Why));
			if (Men.IsEmpty())
			{
				return Finish(State, false);
			}
			State.Fighter = Men[0];
			Check(State, !Men[0]->IsPlayerTeam(), TEXT("he is not on the player's team"));
			State.Open = Spots[0];
			// The charges: control (ordinary) and owned, a mine and a wire each, 15 m apart from each other.
			const FVector Base = Spots[1];
			State.ControlMine = SpawnMine(*World, Base, false);
			State.OwnedMine = SpawnMine(*World, Spots[2], true);
			FVector WireA;
			FVector WireB;
			const bool bWireSpots = NavPointAt(*Nav, Base, 1000.f, 90.f, GroundZ, WireA) && NavPointAt(*Nav, Spots[2], 1000.f, 90.f, GroundZ, WireB);
			if (bWireSpots)
			{
				State.ControlWire = SpawnWire(*World, WireA, false);
				State.OwnedWire = SpawnWire(*World, WireB, true);
			}
			Check(State, State.ControlMine.IsValid() && State.OwnedMine.IsValid() && State.ControlWire.IsValid() && State.OwnedWire.IsValid(),
				TEXT("two mines (ordinary / owned) and two PMC wires (ordinary / owned) spawned"));
			Check(State, State.OwnedMine.IsValid() && DeployableRules::IsPmcOwned(*State.OwnedMine) && State.ControlMine.IsValid() && !DeployableRules::IsPmcOwned(*State.ControlMine),
				TEXT("only the adopted charge carries the owner tag"));
			if (!State.ControlMine.IsValid() || !State.OwnedMine.IsValid() || !State.ControlWire.IsValid() || !State.OwnedWire.IsValid())
			{
				return Finish(State, false);
			}
			State.MineRadius = State.OwnedMine->TriggerRadius;
			UE_LOG(LogCodexTactics, Display, TEXT("[PmcOwnedChargesSmoke] fighter spot %s, control mine %s, owned mine %s, control wire %s, owned wire %s"), *Spots[0].ToCompactString(),
				*State.ControlMine->GetActorLocation().ToCompactString(), *State.OwnedMine->GetActorLocation().ToCompactString(),
				*State.ControlWire->GetActorLocation().ToCompactString(), *State.OwnedWire->GetActorLocation().ToCompactString());
			Next(1);
			return true;
		}
		case 1:
		{
			// The PMC fighter stands on the ordinary mine: it blows (today's rule, the control).
			AOperativeCharacter* Fighter = EnsureFighter(State, *Encounters, *Pmc);
			if (!Fighter)
			{
				Check(State, false, TEXT("the fighter lives"));
				return Finish(State, false);
			}
			if (InStage < 0.5)
			{
				return true;
			}
			if (InStage < 0.6 && State.ControlMine.IsValid())
			{
				Fighter->TeleportTo(State.ControlMine->GetActorLocation() + FVector(0.f, 0.f, 95.f), Fighter->GetActorRotation(), false, true);
				return true;
			}
			if (State.ControlMine.IsValid() && InStage < 4.0)
			{
				Fighter->TeleportTo(State.ControlMine->GetActorLocation() + FVector(0.f, 0.f, 95.f), Fighter->GetActorRotation(), false, true);
				return true;
			}
			Check(State, !State.ControlMine.IsValid(), TEXT("control: a PMC fighter on an ordinary level mine sets it off"));
			Next(2);
			return true;
		}
		case 2:
		{
			// ... and on the ordinary PMC wire.
			AOperativeCharacter* Fighter = EnsureFighter(State, *Encounters, *Pmc);
			if (!Fighter || !State.ControlWire.IsValid())
			{
				Check(State, false, TEXT("the control wire exists"));
				return Finish(State, false);
			}
			const FVector Spot = State.ControlWire->GetActorLocation() - FVector(0.f, 0.f, 30.f) + FVector(0.f, 0.f, 95.f);
			if (InStage < 3.0 && !State.ControlWire->IsTripped())
			{
				Fighter->TeleportTo(Spot, Fighter->GetActorRotation(), false, true);
				return true;
			}
			Check(State, State.ControlWire->IsTripped(), TEXT("control: a PMC fighter on an ordinary PMC wire trips it"));
			Next(3);
			return true;
		}
		case 3:
		{
			// The owned mine: the fighter stands on it for 3 s, nothing happens.
			AOperativeCharacter* Fighter = EnsureFighter(State, *Encounters, *Pmc);
			if (!Fighter || !State.OwnedMine.IsValid())
			{
				Check(State, false, TEXT("the owned mine exists"));
				return Finish(State, false);
			}
			Fighter->TeleportTo(State.OwnedMine->GetActorLocation() + FVector(0.f, 0.f, 95.f), Fighter->GetActorRotation(), false, true);
			if (InStage < 3.0)
			{
				return true;
			}
			Check(State, State.OwnedMine.IsValid() && State.OwnedMine->IsArmedNow(), TEXT("a PMC fighter on the PMC's own mine: it stays armed and does not blow"));
			Next(4);
			return true;
		}
		case 4:
		{
			AOperativeCharacter* Fighter = EnsureFighter(State, *Encounters, *Pmc);
			if (!Fighter || !State.OwnedWire.IsValid())
			{
				Check(State, false, TEXT("the owned wire exists"));
				return Finish(State, false);
			}
			const FVector Spot = State.OwnedWire->GetActorLocation() - FVector(0.f, 0.f, 30.f) + FVector(0.f, 0.f, 95.f);
			Fighter->TeleportTo(Spot, Fighter->GetActorRotation(), false, true);
			if (InStage < 3.0)
			{
				return true;
			}
			Check(State, State.OwnedWire.IsValid() && !State.OwnedWire->IsTripped() && State.OwnedWire->IsArmed(),
				TEXT("a PMC fighter on the PMC's own wire: it stays armed and does not trip"));
			// Out of the way before the player's man steps on them.
			Fighter->TeleportTo(State.Open + FVector(0.f, 0.f, 95.f), Fighter->GetActorRotation(), false, true);
			Next(5);
			return true;
		}
		case 5:
		{
			// The player's side still sets the owned wire off, then the owned mine (the player's rules are unchanged).
			if (!Mate || !State.OwnedWire.IsValid())
			{
				Check(State, false, TEXT("the player's man and the owned wire exist"));
				return Finish(State, false);
			}
			const FVector Spot = State.OwnedWire->GetActorLocation() - FVector(0.f, 0.f, 30.f) + FVector(0.f, 0.f, 95.f);
			Mate->SetStance(EOperativeStance::Standing); // a prone body passes under the wire; no walking off the spot
			Mate->StopOperative();
			if (InStage < 0.15)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("[PmcOwnedChargesSmoke] the player's man goes to the owned wire at %s (owned mine %s)"), *Spot.ToCompactString(),
					State.OwnedMine.IsValid() ? *State.OwnedMine->GetActorLocation().ToCompactString() : TEXT("gone"));
			}
			if (InStage < 3.0 && !State.OwnedWire->IsTripped())
			{
				Mate->TeleportTo(Spot, Mate->GetActorRotation(), false, true);
				return true;
			}
			Check(State, State.OwnedWire->IsTripped(), TEXT("the player's man on the owned wire trips it"));
			Next(6);
			return true;
		}
		case 6:
		{
			if (!Mate)
			{
				Check(State, false, TEXT("the player's man exists"));
				return Finish(State, false);
			}
			if (State.OwnedMine.IsValid() && InStage < 3.0)
			{
				Mate->SetStance(EOperativeStance::Standing);
				Mate->StopOperative();
				Mate->TeleportTo(State.OwnedMine->GetActorLocation() + FVector(0.f, 0.f, 95.f), Mate->GetActorRotation(), false, true);
				return true;
			}
			Check(State, !State.OwnedMine.IsValid(), TEXT("the player's man on the owned mine sets it off"));
			Next(7);
			return true;
		}
		case 7:
		{
			// Path avoidance: a fresh owned mine in the middle of an open straight run.
			const FVector Centre = Mate ? Mate->GetActorLocation() : State.Open;
			const float GroundZ = static_cast<float>(Centre.Z) - 92.f;
			bool bFound = false;
			for (int32 Bearing = 0; Bearing < 360 && !bFound; Bearing += 15)
			{
				FVector From;
				FVector To;
				if (!NavPointAt(*Nav, Centre, 2500.f, static_cast<float>(Bearing), GroundZ, From) || !NavPointAt(*Nav, From, 3000.f, static_cast<float>(Bearing) + 90.f, GroundZ, To))
				{
					continue;
				}
				TArray<FVector> Straight;
				if (!PathPoints(*World, *Nav, From, To, nullptr, Straight))
				{
					continue;
				}
				const FVector Middle = (From + To) * 0.5f;
				FNavLocation OnNav;
				// Open ground at the middle: the navmesh is there and 4 m to either side of the line too.
				const FVector Side = (To - From).GetSafeNormal2D().RotateAngleAxis(90.f, FVector::UpVector) * 400.f;
				FNavLocation Left;
				FNavLocation Right;
				if (!Nav->ProjectPointToNavigation(Middle, OnNav, FVector(100.f, 100.f, 200.f)) || !Nav->ProjectPointToNavigation(OnNav.Location + Side, Left, FVector(60.f, 60.f, 200.f))
					|| !Nav->ProjectPointToNavigation(OnNav.Location - Side, Right, FVector(60.f, 60.f, 200.f)) || PathMissCm(Straight, OnNav.Location) > 150.f)
				{
					continue;
				}
				State.PathFrom = From;
				State.PathTo = To;
				State.PathCharge = OnNav.Location;
				bFound = true;
			}
			Check(State, bFound, TEXT("an open straight run found for the path test"));
			if (!bFound)
			{
				return Finish(State, false);
			}
			State.PathMine = SpawnMine(*World, State.PathCharge, true);
			Check(State, State.PathMine.IsValid(), TEXT("an owned mine spawned on the run"));
			if (!State.PathMine.IsValid())
			{
				return Finish(State, false);
			}
			Next(8);
			return true;
		}
		case 8:
		{
			// The navmesh takes the area in (dynamic modifiers rebuild tiles over a few frames).
			if (Nav->IsNavigationBeingBuilt(World) && InStage < 20.0)
			{
				return true;
			}
			if (InStage < 2.0)
			{
				return true;
			}
			TArray<FVector> PlayerPath;
			TArray<FVector> PmcPath;
			const bool bPlayer = PathPoints(*World, *Nav, State.PathFrom, State.PathTo, nullptr, PlayerPath);
			const bool bPmc = PathPoints(*World, *Nav, State.PathFrom, State.PathTo, PmcEncounterNav::FilterFor(false, false), PmcPath);
			Check(State, bPlayer && bPmc, TEXT("both paths exist"));
			if (bPlayer && bPmc)
			{
				const float PlayerMiss = PathMissCm(PlayerPath, State.PathCharge);
				const float PmcMiss = PathMissCm(PmcPath, State.PathCharge);
				Check(State, PlayerMiss <= State.MineRadius, FString::Printf(TEXT("the player's (default) path is not steered round the hidden owned mine (passes %.0f cm from it, trigger %.0f)"),
					PlayerMiss, State.MineRadius));
				Check(State, PmcMiss > State.MineRadius, FString::Printf(TEXT("the PMC fighters' path goes round it (passes %.0f cm from it, trigger %.0f)"), PmcMiss, State.MineRadius));
			}
			// Spotted by the squad: the known-trap area is added over the PMC one; the PMC still goes round.
			if (State.PathMine.IsValid())
			{
				State.PathMine->Reveal();
			}
			Next(9);
			return true;
		}
		case 9:
		{
			if (Nav->IsNavigationBeingBuilt(World) && InStage < 20.0)
			{
				return true;
			}
			if (InStage < 2.0)
			{
				return true;
			}
			TArray<FVector> PmcPath;
			const bool bPmc = PathPoints(*World, *Nav, State.PathFrom, State.PathTo, PmcEncounterNav::FilterFor(false, false), PmcPath);
			Check(State, bPmc && PathMissCm(PmcPath, State.PathCharge) > State.MineRadius,
				FString::Printf(TEXT("still round the spotted owned mine (passes %.0f cm from it)"), bPmc ? PathMissCm(PmcPath, State.PathCharge) : -1.f));
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
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.PmcOwnedChargesSmoke"),
		TEXT("Dev check (Depot + L_Gen_Depot_1.json): PMC-owned level charges (owner tag + nav area): PMC fighters do not set them off and path round them, ")
		TEXT("the player's men still do / do not; ordinary charges unchanged; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
