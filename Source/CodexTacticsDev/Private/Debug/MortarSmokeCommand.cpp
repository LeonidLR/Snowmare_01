// Dev-only console command for a headless check of the mortar nests (2026-11-09; docs/design/SQUAD_SIX_FIRE_TEAMS.md 7.3; docs/systems/weapons.md
// "Mortar nests") on L_MovementTest:
//   Scripts/smoke_parallel.ps1 -Commands MortarSmoke -Jobs 2 -NoRetry -ExtraArgs "-Multiprocess"
// Builds a range 30 m above the leader (a floor slab, target dummies: boxes with a deep health pool; one under a roof), puts the squad's nest at
// the range's start with the whole squad as its crew, and checks:
//   1. the crew of three fires at a point 80 m away that the crew sees: high-angle rounds with a long flight, each landing closer (the
//      bracketing walk), the open dummy hurt, the dummy under the roof untouched (a plunging blast: only a roof protects);
//   2. one man walks off -> the crew of two stops firing, and a new order is refused (crew);
//   3. a point behind a tall wall that nobody of the squad sees is refused (the spotter rule);
//   4. a PMC nest with a PMC crew 70 m away fires at the spotted squad by its commander's own decision (pmc_commander.json mortar_*).
// Logs PASS/FAIL, then exits.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/MortarNestActor.h"
#include "Combat/MortarRules.h"
#include "Combat/MortarSubsystem.h"
#include "Combat/ProjectileSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

namespace MortarSmoke
{
	constexpr float WarmupSeconds = 3.f;
	constexpr float StepSeconds = 0.1f;
	constexpr float StageTimeout = 40.f;
	constexpr float RangeHeight = 3000.f;
	constexpr int32 RoundsToWatch = 4;

	struct FState
	{
		int32 Stage = 0;
		float StageTime = 0.f;
		bool bAllOk = true;
		FVector O = FVector::ZeroVector;
		FVector F = FVector::ForwardVector;
		FVector R = FVector::RightVector;
		FVector Aim = FVector::ZeroVector;
		TWeakObjectPtr<AMortarNestActor> Nest;
		TWeakObjectPtr<AMortarNestActor> PmcNest;
		TWeakObjectPtr<AActor> OpenDummy;
		TWeakObjectPtr<AActor> RoofDummy;
		TArray<TWeakObjectPtr<AOperativeCharacter>> Crew;
		TArray<FMortarShot> Shots;
		TArray<FMortarShot> PmcShots;
		TArray<FProjectileImpact> Impacts;
		FDelegateHandle ImpactHandle;
		int32 RoundsBefore = 0;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok") : TEXT("FAILED"), *What);
		State.bAllOk &= bOk;
	}

	void Finish(UWorld* World, FState& State)
	{
		if (UProjectileSubsystem* Projectiles = World ? World->GetSubsystem<UProjectileSubsystem>() : nullptr)
		{
			Projectiles->OnProjectileFinished.Remove(State.ImpactHandle);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.bAllOk ? TEXT("PASS") : TEXT("FAIL"));
		State.Stage = -1;
		FPlatformMisc::RequestExit(false, TEXT("MortarSmoke"));
	}

	FVector At(const FState& State, float Along, float Side, float Up = 0.f)
	{
		return State.O + State.F * Along + State.R * Side + FVector(0.f, 0.f, Up);
	}

	AStaticMeshActor* SpawnBlock(UWorld* World, const FState& State, const FVector& Centre, const FVector& SizeCm)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		if (!Cube)
		{
			return nullptr;
		}
		const FTransform Transform(FRotator(0.f, State.F.Rotation().Yaw, 0.f), Centre, SizeCm / 100.f);
		AStaticMeshActor* Block = World->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Block)
		{
			return nullptr;
		}
		Block->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Block->GetStaticMeshComponent()->SetStaticMesh(Cube);
		Block->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
		Block->FinishSpawning(Transform);
		return Block;
	}

	AActor* SpawnDummy(UWorld* World, const FState& State, const FVector& Feet)
	{
		AStaticMeshActor* Dummy = SpawnBlock(World, State, Feet + FVector(0.f, 0.f, 90.f), FVector(100.f, 100.f, 180.f));
		if (Dummy)
		{
			UHealthComponent* Health = NewObject<UHealthComponent>(Dummy, TEXT("DummyHealth"));
			Dummy->AddInstanceComponent(Health);
			Health->RegisterComponent();
			Health->SetMaxHealth(100000.f, true);
		}
		return Dummy;
	}

	void Enter(FState& State, int32 Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
	}

	/** The blast's entry for Actor in Impact (null = not reached). */
	const FExplosionVictim* VictimIn(const FProjectileImpact& Impact, const AActor* Actor)
	{
		return Impact.Blast.Victims.FindByPredicate([Actor](const FExplosionVictim& Victim) { return Actor && Victim.Actor.Get() == Actor; });
	}

	/** Returns false when finished. */
	bool Step(UWorld* World, FState& State)
	{
		State.StageTime += StepSeconds;
		AMortarNestActor* Nest = State.Nest.Get();
		UMortarSubsystem* Mortars = UMortarSubsystem::Get(World);
		if (!Nest || !Mortars || State.StageTime > StageTimeout)
		{
			Check(State, false, FString::Printf(TEXT("stage %d timed out (rounds %d, impacts %d)"), State.Stage, State.Shots.Num(), State.Impacts.Num()));
			Finish(World, State);
			return false;
		}
		switch (State.Stage)
		{
		case 0: // the crew settles
		{
			if (State.StageTime < 1.f)
			{
				return true;
			}
			Check(State, Nest->GetCrewAtNest() == 3, FString::Printf(TEXT("0: a crew of three at the nest (%d at it, %d assigned)"), Nest->GetCrewAtNest(),
				Nest->GetCrewAssigned()));
			AOperativeCharacter* Spotter = nullptr;
			const EMortarRefusal Check0 = Nest->CheckFireAt(State.Aim, &Spotter);
			Check(State, Spotter != nullptr, FString::Printf(TEXT("1: the crew sees the point 80 m away (spotter %s)"), Spotter ? *Spotter->DisplayName.ToString() : TEXT("none")));
			const EMortarRefusal Refusal = Nest->OrderFire(State.Aim);
			Check(State, Refusal == EMortarRefusal::None, FString::Printf(TEXT("1: the fire order at 80 m accepted (%s)"), *MortarRules::RefusalText(Refusal)));
			if (Refusal != EMortarRefusal::None || Check0 != EMortarRefusal::None)
			{
				Finish(World, State);
				return false;
			}
			Enter(State, 1);
			return true;
		}

		case 1: // the rounds walk in
		{
			TArray<FProjectileImpact> Ours = State.Impacts.FilterByPredicate([&Nest](const FProjectileImpact& Impact) { return Impact.SourceName.EndsWith(Nest->NestId); });
			if (Ours.Num() < RoundsToWatch)
			{
				return true;
			}
			Nest->CeaseFire();
			bool bWalkIn = State.Shots.Num() >= RoundsToWatch;
			FString Errors;
			for (int32 Index = 0; Index < State.Shots.Num() && Index < RoundsToWatch; ++Index)
			{
				Errors += FString::Printf(TEXT(" %.1f"), State.Shots[Index].ErrorCm / 100.f);
				bWalkIn &= Index == 0 || State.Shots[Index].ErrorCm < State.Shots[Index - 1].ErrorCm;
			}
			Check(State, bWalkIn, FString::Printf(TEXT("1: each round aimed closer (m off:%s)"), *Errors));
			FString Landed;
			float MinFlight = 1e9f;
			for (const FProjectileImpact& Impact : Ours)
			{
				Landed += FString::Printf(TEXT(" %.1f"), FVector::Dist2D(Impact.Location, State.Aim) / 100.f);
				MinFlight = FMath::Min(MinFlight, Impact.FlightSeconds);
			}
			const float First = static_cast<float>(FVector::Dist2D(Ours[0].Location, State.Aim));
			const float Last = static_cast<float>(FVector::Dist2D(Ours.Last().Location, State.Aim));
			Check(State, Last < First && Last < 600.f, FString::Printf(TEXT("1: the rounds walked in (m from the aim point:%s)"), *Landed));
			Check(State, MinFlight > 3.f, FString::Printf(TEXT("1: a long plunging flight (%.1f s at least)"), MinFlight));
			float OpenDamage = 0.f;
			int32 RoofReached = 0;
			float RoofDamage = 0.f;
			for (const FProjectileImpact& Impact : Ours)
			{
				if (const FExplosionVictim* Open = VictimIn(Impact, State.OpenDummy.Get()))
				{
					OpenDamage += Open->Damage;
				}
				if (const FExplosionVictim* Roofed = VictimIn(Impact, State.RoofDummy.Get()))
				{
					++RoofReached;
					RoofDamage += Roofed->Damage;
				}
			}
			Check(State, OpenDamage > 0.f, FString::Printf(TEXT("1: the dummy in the open hurt (%.1f)"), OpenDamage));
			Check(State, RoofReached > 0 && RoofDamage <= 0.01f, FString::Printf(TEXT("1: the dummy under a roof reached %d times, hurt %.2f (a roof protects)"),
				RoofReached, RoofDamage));
			// 2. One man walks off while the nest fires.
			Nest->OrderFire(State.Aim);
			State.RoundsBefore = Nest->GetRoundsFired();
			Enter(State, 2);
			return true;
		}

		case 2: // the order's first round leaves, then one man walks off
			if (Nest->GetRoundsFired() == State.RoundsBefore && State.StageTime < 6.f)
			{
				return true;
			}
			if (AOperativeCharacter* Third = State.Crew.Num() > 2 ? State.Crew[2].Get() : nullptr)
			{
				// He walks off 6 m (the sky range has no navmesh for a walk: the same distance check as a walk that leaves crew_leave_m behind).
				Third->TeleportTo(Third->GetActorLocation() - State.F * 600.f, Third->GetActorRotation(), false, true);
			}
			State.RoundsBefore = Nest->GetRoundsFired();
			Enter(State, 3);
			return true;

		case 3:
			if (State.StageTime < 2.f * MortarRules::Get().IntervalSeconds)
			{
				return true;
			}
			Check(State, Nest->GetCrewAtNest() == 2 && !Nest->IsFiring() && Nest->GetRoundsFired() <= State.RoundsBefore + 1
				&& Nest->GetLastRefusal() == EMortarRefusal::Crew, FString::Printf(TEXT("2: a man walked off - the crew of %d stopped firing (rounds %d -> %d, refusal \"%s\")"),
					Nest->GetCrewAtNest(), State.RoundsBefore, Nest->GetRoundsFired(), *MortarRules::RefusalText(Nest->GetLastRefusal())));
			Check(State, Nest->OrderFire(State.Aim) == EMortarRefusal::Crew, TEXT("2: a crew of two cannot fire"));
			// 3. The third man back; a point behind a tall wall nobody sees.
			if (AOperativeCharacter* Third = State.Crew.Num() > 2 ? State.Crew[2].Get() : nullptr)
			{
				Nest->AssignCrew(Third, true);
			}
			SpawnBlock(World, State, At(State, 6000.f, -3000.f, 600.f), FVector(60.f, 2500.f, 1200.f));
			Enter(State, 4);
			return true;

		case 4:
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			const FVector Hidden = At(State, 9000.f, -3000.f);
			const EMortarRefusal Refusal = Nest->OrderFire(Hidden);
			Check(State, Nest->GetCrewAtNest() == 3 && Refusal == EMortarRefusal::NotSpotted,
				FString::Printf(TEXT("3: a point behind a wall nobody sees is refused (crew %d, \"%s\")"), Nest->GetCrewAtNest(), *MortarRules::RefusalText(Refusal)));
			Check(State, Nest->OrderFire(At(State, 1500.f, 0.f)) == EMortarRefusal::TooClose, TEXT("3: a point 15 m away is inside the minimum range"));
			Nest->CeaseFire();
			// 4. A PMC nest with its PMC crew 70 m ahead, facing the squad.
			FMortarNestPlacement Pmc;
			Pmc.Id = TEXT("SMOKE_PMC");
			Pmc.Location = At(State, 7000.f, 1500.f);
			Pmc.bHasZ = true;
			Pmc.YawDeg = (-State.F).Rotation().Yaw;
			Pmc.TeamId = -1;
			Pmc.Crew = 3;
			AMortarNestActor* PmcNest = Mortars->SpawnNest(Pmc);
			State.PmcNest = PmcNest;
			if (PmcNest)
			{
				for (AOperativeCharacter* Man : PmcNest->GetCrew())
				{
					Man->HealthComponent->SetMaxHealth(100000.f, true); // the squad's rifles must not break the crew in this check
				}
			}
			Check(State, PmcNest && PmcNest->TeamId > 0 && PmcNest->GetCrewAtNest() == 3, FString::Printf(TEXT("4: a PMC nest (team %d) with a PMC crew of %d"),
				PmcNest ? PmcNest->TeamId : -1, PmcNest ? PmcNest->GetCrewAtNest() : 0));
			for (const TWeakObjectPtr<AOperativeCharacter>& Man : State.Crew)
			{
				if (Man.IsValid())
				{
					Man->HealthComponent->SetMaxHealth(100000.f, true);
				}
			}
			Enter(State, 5);
			return true;
		}

		case 5:
		{
			AMortarNestActor* PmcNest = State.PmcNest.Get();
			if (!PmcNest || PmcNest->GetRoundsFired() == 0)
			{
				return true;
			}
			const FVector Target = PmcNest->GetFireTarget();
			float Nearest = 1e9f;
			for (const TWeakObjectPtr<AOperativeCharacter>& Man : State.Crew)
			{
				Nearest = Man.IsValid() ? FMath::Min(Nearest, static_cast<float>(FVector::Dist2D(Man->GetActorLocation(), Target))) : Nearest;
			}
			Check(State, Nearest < 1000.f, FString::Printf(TEXT("4: the PMC nest fired at the spotted squad by its own decision (%d rounds, the target %.1f m from a squad man, ammo %d)"),
				PmcNest->GetRoundsFired(), Nearest / 100.f, PmcNest->Ammo));
			PmcNest->CeaseFire();
			Finish(World, State);
			return false;
		}

		default:
			return false;
		}
	}

	void Start(UWorld* World)
	{
		TSharedRef<FState> State = MakeShared<FState>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UMortarSubsystem* Mortars = UMortarSubsystem::Get(World);
		UProjectileSubsystem* Projectiles = World->GetSubsystem<UProjectileSubsystem>();
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!Leader || !Mortars || !Projectiles || Squad->GetMembers().Num() < 3)
		{
			Check(*State, false, TEXT("a squad of three, the mortar and projectile subsystems"));
			Finish(World, *State);
			return;
		}
		State->F = FVector(Leader->GetActorForwardVector().X, Leader->GetActorForwardVector().Y, 0.f).GetSafeNormal();
		State->R = FVector::CrossProduct(FVector::UpVector, State->F);
		State->O = Leader->GetActorLocation() + FVector(0.f, 0.f, RangeHeight);
		SpawnBlock(World, *State, At(*State, 5000.f, 0.f, -10.f), FVector(16000.f, 8000.f, 20.f));
		// The aim point: open ground 80 m ahead; the open dummy 1.5 m past it, the roofed one 4 m beside it.
		State->Aim = At(*State, 8000.f, 0.f);
		State->OpenDummy = SpawnDummy(World, *State, At(*State, 8150.f, 0.f));
		State->RoofDummy = SpawnDummy(World, *State, At(*State, 8000.f, 400.f));
		SpawnBlock(World, *State, At(*State, 8000.f, 400.f, 260.f), FVector(400.f, 400.f, 20.f));
		// The squad's nest at the start, the whole squad its crew.
		FMortarNestPlacement Placement;
		Placement.Id = TEXT("SMOKE_SQUAD");
		Placement.Location = State->O;
		Placement.bHasZ = true;
		Placement.YawDeg = State->F.Rotation().Yaw;
		AMortarNestActor* Nest = Mortars->SpawnNest(Placement);
		State->Nest = Nest;
		if (Nest)
		{
			Nest->SetSeed(1109);
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (State->Crew.Num() < 3 && Nest->AssignCrew(Member, true) >= 0)
				{
					State->Crew.Add(Member);
				}
			}
			TWeakPtr<FState> WeakState = State;
			Nest->OnRoundFired.AddLambda([WeakState](AMortarNestActor*, const FMortarShot& Shot)
			{
				if (const TSharedPtr<FState> Pinned = WeakState.Pin())
				{
					Pinned->Shots.Add(Shot);
				}
			});
		}
		TWeakPtr<FState> WeakState = State;
		State->ImpactHandle = Projectiles->OnProjectileFinished.AddLambda([WeakState](const FProjectileImpact& Impact)
		{
			if (const TSharedPtr<FState> Pinned = WeakState.Pin())
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke impact (%s): end %d at %s, %.2f s, %d victims"), *Impact.SourceName, static_cast<int32>(Impact.End),
					*Impact.Location.ToCompactString(), Impact.FlightSeconds, Impact.Blast.Victims.Num());
				Pinned->Impacts.Add(Impact);
			}
		});
		Enter(*State, 0);
		TSharedRef<FTimerHandle> Handle = MakeShared<FTimerHandle>();
		TWeakObjectPtr<UWorld> WeakWorld(World);
		World->GetTimerManager().SetTimer(*Handle, FTimerDelegate::CreateLambda([WeakWorld, State, Handle]()
		{
			UWorld* W = WeakWorld.Get();
			if (W && !Step(W, *State))
			{
				W->GetTimerManager().ClearTimer(*Handle);
			}
		}), StepSeconds, true);
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakWorld]()
		{
			if (UWorld* W = WeakWorld.Get())
			{
				Start(W);
			}
		}), WarmupSeconds, false);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.MortarSmoke"),
		TEXT("Dev check: mortar nests (a crew of 3 walks rounds in on a spotted point 80 m away, a roof protects, a crew of 2 / an unspotted point refused, a PMC nest shells the spotted squad); logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
