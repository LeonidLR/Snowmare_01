// Dev-only headless check of the MANOEUVRE UNDER FIRE (bug #43 rest, 2026-11-07, UnderFireRules; docs/systems/pmc-squad-ai.md "Under fire")
// on L_Gen_Depot_1 - the user's case:
//   Scripts/smoke_parallel.ps1 -Commands UnderFireManoeuvreSmoke
// In an empty yard of the Depot a 3.6 m wide, 22 m long passage between two 3 m walls. The squad (formation, AGGRESSIVE) is ordered into the passage and halts there in a column; a PMC fighter at the passage's
// far end, behind a 1.2 m wall (his rifle over it), fires at the column (the smoke's rounds, every 0.5 s); a 1 m crate stands right in
// front of the rearmost man (the only cover in reach). Checks:
//  A. the rear men have a mate in their line to the flash at first; within 8 s of fire they step off the line (<= 2 m, inside the passage)
//     or take cover, and each rear man returns fire; someone takes cover (the crate); nobody leaves the passage / walks > 3 m off;
//  B. a new move order to the leader releases the followers' held spots at once (never over the player's order);
//  C. the same rule for the PMC: a second fighter standing in the open past the passage's mouth, a crate 1.5 m from him, is fired at by
//     the leader - he takes cover behind the crate (no commander: the shared rule).
// Nothing is saved.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/PmcEncounterSubsystem.h"
#include "AI/PmcSquadSubsystem.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/CombatPressureSubsystem.h"
#include "Combat/EnemyRegistrySubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/UnderFireRules.h"
#include "Components/StaticMeshComponent.h"
#include "Data/WeaponDataAsset.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Debug/SmokeUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Environment/EnvironmentSubsystem.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"

namespace UnderFireManoeuvreSmoke
{
	constexpr float StepSeconds = 0.05f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		FVector O = FVector::ZeroVector;
		FVector F = FVector::ForwardVector;
		FVector R = FVector::RightVector;
		TArray<TWeakObjectPtr<AOperativeCharacter>> Members;
		TWeakObjectPtr<AOperativeCharacter> Shooter;
		TWeakObjectPtr<AOperativeCharacter> Exposed;
		TArray<FVector> Halt;
		TArray<double> LastShot;
		TArray<int32> Shots;
		TArray<float> MaxOffCm;
		bool bMateInLineAtStart[3] = { false, false, false };
		float NextShot = 0.f;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("UnderFireManoeuvreSmoke"));
		return false;
	}

	void Next(FState& State)
	{
		++State.Stage;
		State.StageTime = 0.f;
	}

	FVector At(const FState& State, float Forward, float Right, float Up = 0.f)
	{
		return State.O + State.F * Forward + State.R * Right + FVector(0.f, 0.f, Up);
	}

	AStaticMeshActor* SpawnUnderFireBlock(UWorld* World, const FVector& Centre, const FRotator& Rotation, const FVector& SizeCm)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		const FTransform Transform(Rotation, Centre, SizeCm / 100.f);
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

	void MakeTough(AOperativeCharacter* Operative)
	{
		if (Operative && Operative->HealthComponent)
		{
			Operative->HealthComponent->SetMaxHealth(100000.f, false);
			Operative->HealthComponent->Heal(100000.f);
		}
	}

	AOperativeCharacter* SpawnFighter(UWorld* World, const FVector& Spot, const FRotator& Facing, int32 Seed)
	{
		UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World);
		const TArray<AOperativeCharacter*> Spawned = Pmc ? Pmc->SpawnSquad(1, SmokeUtils::FreeSpot(World, Spot, nullptr), Facing, 1, Seed) : TArray<AOperativeCharacter*>();
		AOperativeCharacter* Fighter = Spawned.IsEmpty() ? nullptr : Spawned[0];
		if (Fighter)
		{
			Pmc->SetTeamCommanded(1, false); // no commander orders: only the shared rules under test act
			MakeTough(Fighter);
		}
		return Fighter;
	}

	bool ClickGround(UWorld* World, const FVector& Point)
	{
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(World->GetFirstPlayerController());
		FHitResult Hit;
		if (!PC || !World->LineTraceSingleByChannel(Hit, Point + FVector(0.f, 0.f, 300.f), Point - FVector(0.f, 0.f, 300.f), ECC_Visibility))
		{
			return false;
		}
		PC->HandleWorldHit(Hit);
		return true;
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
		if (State.Time > 150.f)
		{
			Check(State, false, FString::Printf(TEXT("timeout in stage %d"), State.Stage));
			return Finish(State, false);
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		// The level's monsters stay out of it (the wave / pressure must not bring targets in).
		for (AEnemyCharacter* Enemy : UEnemyRegistrySubsystem::GetEnemyCharacters(World))
		{
			if (Enemy && Enemy->IsActorTickEnabled())
			{
				SmokeUtils::ParkEnemyFarAway(Enemy);
				Enemy->SetActorTickEnabled(false);
			}
		}
		switch (State.Stage)
		{
		case 0:
		{
			if (State.Time < 3.f)
			{
				return true;
			}
			if (!Squad || !Flow || !UPmcSquadSubsystem::Get(World) || Squad->GetMembers().Num() < 3 || !Squad->GetLeader())
			{
				Check(State, false, TEXT("squad of 3, game flow, PMC subsystem"));
				return Finish(State, false);
			}
			if (UCombatPressureSubsystem* Pressure = World->GetSubsystem<UCombatPressureSubsystem>())
			{
				Pressure->SetAutoSpawn(false);
				Pressure->DespawnAll();
			}
			if (UPmcEncounterSubsystem* Encounters = UPmcEncounterSubsystem::Get(World))
			{
				Encounters->CancelPending();
			}
			if (UEnvironmentSubsystem* Environment = UEnvironmentSubsystem::Get(World))
			{
				Environment->SetClockScale(0.f);
				Environment->ForceWeather(EWeatherState::Clear, true);
				Environment->SetHour(12.f);
			}
			TArray<AOperativeCharacter*> Roster = Squad->GetMembers();
			Roster.Sort([](const AOperativeCharacter& A, const AOperativeCharacter& B) { return A.SquadIndex < B.SquadIndex; });
			Squad->SetLeaderByIndex(0);
			TArray<const AActor*> Ignore;
			for (const AOperativeCharacter* Member : Roster)
			{
				Ignore.Add(Member);
			}
			AOperativeCharacter* Leader = Roster[0];
			const FVector Feet = Leader->GetActorLocation() - FVector(0.f, 0.f, Leader->GetSimpleCollisionHalfHeight());
			const SmokeUtils::FClearArena Arena = SmokeUtils::FindClearArena(World, Feet, Leader->GetActorForwardVector(),
				FBox2D(FVector2D(-1200.f, -700.f), FVector2D(2700.f, 700.f)), Ignore);
			Check(State, Arena.bFound, FString::Printf(TEXT("an empty yard on the Depot (tried %d)"), Arena.Tried));
			if (!Arena.bFound)
			{
				return Finish(State, false);
			}
			State.O = Arena.Origin;
			State.F = Arena.Forward;
			State.R = Arena.Right;
			const FRotator Along = State.F.Rotation();
			// The passage: inner faces at R -180 / +180, F 0..2200 (the crate comes once the column has halted, stage 2).
			bool bBlocks = SpawnUnderFireBlock(World, At(State, 1100.f, 195.f, 150.f), Along, FVector(2200.f, 30.f, 300.f)) != nullptr;
			bBlocks &= SpawnUnderFireBlock(World, At(State, 1100.f, -195.f, 150.f), Along, FVector(2200.f, 30.f, 300.f)) != nullptr;
			// The far end: a 1.2 m wall the fighter fires over; part C's crate past the mouth, right side.
			bBlocks &= SpawnUnderFireBlock(World, At(State, 2480.f, 0.f, 60.f), Along, FVector(30.f, 300.f, 120.f)) != nullptr;
			bBlocks &= SpawnUnderFireBlock(World, At(State, 2350.f, 480.f, 50.f), Along, FVector(50.f, 160.f, 100.f)) != nullptr;
			Check(State, bBlocks, TEXT("the passage (3.6 m x 22 m, 3 m walls), the far wall, part C's crate"));
			SmokeUtils::StartWaveFight(Flow);
			if (Squad->IsSoloMode())
			{
				Squad->ExitSoloMode();
			}
			Squad->SetSquadPosture(ESquadFirePosture::Aggressive);
			Squad->SetFollowersHolding(false);
			for (int32 Index = 0; Index < 3; ++Index)
			{
				AOperativeCharacter* Member = Roster[Index];
				Member->StopOperative();
				Member->ColdLevel = 0.f;
				Member->SetStance(EOperativeStance::Standing);
				MakeTough(Member);
				const FVector Spot = SmokeUtils::FreeSpot(World, At(State, -500.f - Index * 250.f, 0.f), Member);
				Member->TeleportTo(FVector(Spot.X, Spot.Y, Spot.Z + 4.f), Along, false, true);
				State.Members.Add(Member);
			}
			Next(State);
			return true;
		}
		case 1:
		{
			if (State.StageTime < 1.5f)
			{
				return true; // the navmesh round the new walls
			}
			Check(State, ClickGround(World, At(State, 1000.f, 0.f)), TEXT("the squad is ordered into the passage (10 m in)"));
			Next(State);
			return true;
		}
		case 2:
		{
			bool bSettled = State.StageTime > 3.f;
			for (const TWeakObjectPtr<AOperativeCharacter>& Member : State.Members)
			{
				bSettled &= Member.IsValid() && Member->GetVelocity().Size2D() < 10.f && !Member->HasOwnMoveOrder();
			}
			if (!bSettled && State.StageTime < 20.f)
			{
				return true;
			}
			FString Column;
			bool bInside = true;
			for (const TWeakObjectPtr<AOperativeCharacter>& Member : State.Members)
			{
				const FVector D = Member->GetActorLocation() - State.O;
				const float Fwd = static_cast<float>(FVector::DotProduct(D, State.F));
				const float Side = static_cast<float>(FVector::DotProduct(D, State.R));
				Column += FString::Printf(TEXT(" %s (F %.0f, R %.0f)"), *Member->DisplayName.ToString(), Fwd, Side);
				bInside &= Fwd > 0.f && Fwd < 2200.f && FMath::Abs(Side) < 180.f;
				State.Halt.Add(Member->GetActorLocation());
				State.LastShot.Add(Member->GetLastShotTime());
				State.Shots.Add(0);
				State.MaxOffCm.Add(0.f);
			}
			Check(State, bSettled && bInside, FString::Printf(TEXT("the column halted in the passage:%s"), *Column));
			// A 1 m crate right in front of the rearmost man (between him and the far end): the only cover in reach that shields from there - the
			// cover search casts rays from the man, so the men ahead of it see only its far face (the flash's side) and find nothing.
			int32 Rearmost = 0;
			for (int32 Index = 1; Index < State.Halt.Num(); ++Index)
			{
				Rearmost = FVector::DotProduct(State.Halt[Index] - State.O, State.F) < FVector::DotProduct(State.Halt[Rearmost] - State.O, State.F) ? Index : Rearmost;
			}
			const FVector RearD = State.Halt[Rearmost] - State.O;
			const float CrateF = static_cast<float>(FVector::DotProduct(RearD, State.F)) + 85.f;
			const float CrateR = FMath::Clamp(static_cast<float>(FVector::DotProduct(RearD, State.R)) - 50.f, -90.f, 90.f);
			Check(State, SpawnUnderFireBlock(World, At(State, CrateF, CrateR, 50.f), State.F.Rotation(), FVector(40.f, 160.f, 100.f)) != nullptr,
				FString::Printf(TEXT("a 1 m crate just ahead of the rearmost man %s (F %.0f, R %.0f)"), *State.Members[Rearmost]->DisplayName.ToString(), CrateF, CrateR));
			AOperativeCharacter* Shooter = SpawnFighter(World, At(State, 2600.f, 0.f), (-State.F).Rotation(), 31);
			Check(State, Shooter != nullptr, TEXT("a PMC fighter behind the 1.2 m wall at the passage's far end"));
			if (!Shooter)
			{
				return Finish(State, false);
			}
			Shooter->SetStance(EOperativeStance::Standing);
			Shooter->bTacticalCeaseFire = true; // his rounds are the smoke's
			State.Shooter = Shooter;
			Next(State);
			return true;
		}
		case 3:
		{
			AOperativeCharacter* Shooter = State.Shooter.Get();
			if (!Shooter)
			{
				return Finish(State, false);
			}
			if (State.StageTime < 2.f)
			{
				return true; // the navmesh round the crate
			}
			if (State.StageTime >= 2.f && State.StageTime - StepSeconds < 2.f)
			{
				// Before the first round: the rear men's lines to his muzzle run through the mates ahead.
				const FVector Flash = Shooter->GetWeaponMuzzleLocation();
				for (int32 Index = 1; Index < 3; ++Index)
				{
					const AOperativeCharacter* Rear = State.Members[Index].Get();
					for (int32 Other = 0; Other < Index && Rear; ++Other)
					{
						const AOperativeCharacter* Mate = State.Members[Other].Get();
						State.bMateInLineAtStart[Index] |= Mate && UnderFireRules::IsMateInLine(Rear->GetActorLocation(), Flash, Mate->GetActorLocation(),
							UnderFireRules::GetConfig().MateBlockRadiusCm);
					}
					UE_LOG(LogCodexTactics, Display, TEXT("Smoke: rear man %d %s has a mate in his line to the flash: %d"), Index,
						Rear ? *Rear->DisplayName.ToString() : TEXT("-"), State.bMateInLineAtStart[Index] ? 1 : 0);
				}
			}
			const float FireTime = State.StageTime - 2.f;
			if (FireTime >= State.NextShot && FireTime < 8.f)
			{
				State.NextShot = FireTime + 0.5f;
				AOperativeCharacter* Victim = State.Members[FMath::FloorToInt(FireTime / 0.5f) % 3].Get();
				Shooter->ShootAtTarget(Victim ? Victim : State.Members[0].Get(), 1.f);
			}
			for (int32 Index = 0; Index < State.Members.Num(); ++Index)
			{
				const AOperativeCharacter* Member = State.Members[Index].Get();
				if (!Member)
				{
					continue;
				}
				if (Member->GetLastShotTime() != State.LastShot[Index])
				{
					State.LastShot[Index] = Member->GetLastShotTime();
					++State.Shots[Index];
				}
				State.MaxOffCm[Index] = FMath::Max(State.MaxOffCm[Index], static_cast<float>(FVector::Dist2D(Member->GetActorLocation(), State.Halt[Index])));
			}
			if (FireTime < 8.5f)
			{
				return true;
			}
			int32 Covers = 0;
			int32 InCover = 0;
			for (int32 Index = 0; Index < State.Members.Num(); ++Index)
			{
				const AOperativeCharacter* Member = State.Members[Index].Get();
				if (!Member)
				{
					continue;
				}
				const FVector D = Member->GetActorLocation() - State.O;
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke: %s: %d shots (%d at the flash), steps %d, cover moves %d, prone drops %d, in cover %d, stance %d, moved %.0f cm, now (F %.0f, R %.0f)"),
					*Member->DisplayName.ToString(), State.Shots[Index], Member->ReturnFireShots, Member->UnderFireSidesteps, Member->UnderFireCoverMoves,
					Member->UnderFireProneDrops, Member->bInCover ? 1 : 0, static_cast<int32>(Member->GetStance()), State.MaxOffCm[Index],
					FVector::DotProduct(D, State.F), FVector::DotProduct(D, State.R));
				Covers += Member->UnderFireCoverMoves;
				InCover += Member->bInCover ? 1 : 0;
				Check(State, State.MaxOffCm[Index] <= 300.f + (Member->UnderFireCoverMoves > 0 ? 500.f : 0.f),
					FString::Printf(TEXT("%s stayed put / moved only a short way (%.0f cm)"), *Member->DisplayName.ToString(), State.MaxOffCm[Index]));
				if (Index >= 1)
				{
					Check(State, Member->UnderFireSidesteps + Member->UnderFireCoverMoves > 0 || !State.bMateInLineAtStart[Index],
						FString::Printf(TEXT("rear man %s (a mate in his line: %d) stepped off the line / took cover"), *Member->DisplayName.ToString(),
							State.bMateInLineAtStart[Index] ? 1 : 0));
					// Main's verification 2026-11-08: the Engineer is shotgun-only now (the D-1 reaches ~15 m) and the shooter stands ~18 m off -
					// beyond a weapon's reach holding fire is right (FindShootTarget's range), within it he must fire back.
					const AOperativeCharacter* FireFrom = State.Shooter.Get();
					const float Reach = Member->CurrentWeapon ? Member->CurrentWeapon->Stats.Range.MaxRangeCm : 1400.f;
					const float ToShooter = FireFrom ? static_cast<float>(FVector::Dist(Member->GetActorLocation(), FireFrom->GetActorLocation())) : 0.f;
					if (ToShooter <= Reach)
					{
						Check(State, State.Shots[Index] > 0, FString::Printf(TEXT("rear man %s got a line and returned fire (%d rounds; the shooter %.0f m off, his reach %.0f m)"),
							*Member->DisplayName.ToString(), State.Shots[Index], ToShooter / 100.f, Reach / 100.f));
					}
					else
					{
						Check(State, State.Shots[Index] == 0, FString::Printf(TEXT("rear man %s: the shooter %.0f m off is beyond his weapon's reach (%.0f m) - he holds fire (%d rounds)"),
							*Member->DisplayName.ToString(), ToShooter / 100.f, Reach / 100.f, State.Shots[Index]));
					}
				}
			}
			Check(State, Covers > 0 && InCover > 0, FString::Printf(TEXT("someone took cover (%d cover moves, %d in cover now)"), Covers, InCover));
			// B: a new order to the leader releases the followers' held spots at once.
			Check(State, ClickGround(World, At(State, 200.f, 0.f)), TEXT("B: the player orders the leader back down the passage"));
			Next(State);
			return true;
		}
		case 4:
		{
			if (State.StageTime < 0.3f)
			{
				return true;
			}
			for (int32 Index = 1; Index < State.Members.Num(); ++Index)
			{
				const AOperativeCharacter* Member = State.Members[Index].Get();
				Check(State, Member && !Member->IsHoldingUnderFireSpot(), FString::Printf(TEXT("B: %s no longer holds his under-fire spot (the leader's new order)"),
					Member ? *Member->DisplayName.ToString() : TEXT("-")));
			}
			// C: the PMC side - a fighter in the open past the passage's mouth, a crate 1.5 m from him between him and the squad.
			if (AOperativeCharacter* Shooter = State.Shooter.Get())
			{
				Shooter->TakeHit(10000000.f, TEXT("Smoke"), false, true);
			}
			AOperativeCharacter* Exposed = SpawnFighter(World, At(State, 2500.f, 480.f), (-State.F).Rotation(), 32);
			Check(State, Exposed != nullptr, TEXT("C: a PMC fighter standing in the open past the passage's mouth, a crate 1.5 m in front of him"));
			if (!Exposed)
			{
				return Finish(State, false);
			}
			Exposed->SetStance(EOperativeStance::Standing);
			Exposed->bTacticalCeaseFire = true;
			State.Exposed = Exposed;
			State.NextShot = 0.f;
			Next(State);
			return true;
		}
		case 5:
		{
			AOperativeCharacter* Exposed = State.Exposed.Get();
			AOperativeCharacter* Leader = State.Members[0].Get();
			if (!Exposed || !Leader)
			{
				return Finish(State, false);
			}
			if (State.StageTime < 1.f)
			{
				return true;
			}
			if (State.StageTime >= State.NextShot && State.StageTime < 6.f)
			{
				State.NextShot = State.StageTime + 0.6f;
				Leader->ShootAtTarget(Exposed, 1.f);
			}
			if (!(Exposed->bInCover || Exposed->HasPendingCover() || Exposed->UnderFireProneDrops > 0) && State.StageTime < 8.f)
			{
				return true;
			}
			if (State.StageTime < 8.f && !Exposed->bInCover)
			{
				return true; // on his way in
			}
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: C: the fighter - cover moves %d, prone drops %d, in cover %d, stance %d"), Exposed->UnderFireCoverMoves,
				Exposed->UnderFireProneDrops, Exposed->bInCover ? 1 : 0, static_cast<int32>(Exposed->GetStance()));
			Check(State, Exposed->UnderFireCoverMoves > 0 && Exposed->bInCover, TEXT("C: the PMC fighter under fire took cover behind the crate (the shared rule)"));
			return Finish(State, true);
		}
		default:
			return Finish(State, false);
		}
	}

	static FAutoConsoleCommandWithWorldAndArgs UnderFireManoeuvreCommand(
		TEXT("CodexTactics.UnderFireManoeuvreSmoke"),
		TEXT("Dev check (bug #43, L_Gen_Depot_1): a column halted in a narrow passage fired at from its far end - the rear men step off the line and return fire, someone takes cover, a new order releases them; a PMC fighter in the open takes cover too; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
		{
			if (!World)
			{
				return;
			}
			// 2026-11-10: the rifle manoeuvre is under test - the Commander's RPG-7 (testing loadout) drawn by the launcher rule knocked the PMC
			// fighter of part C down with a rocket (no cover move then). The launcher's own rule has RpgAiSmoke / RpgPmcSmoke.
			if (IConsoleVariable* LauncherAI = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Combat.LauncherAI")))
			{
				LauncherAI->Set(0, ECVF_SetByConsole);
			}
			TWeakObjectPtr<UWorld> WeakWorld(World);
			TSharedRef<FState> State = MakeShared<FState>();
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float) { return Step(WeakWorld, *State); }), StepSeconds);
		}));
}

#endif // !UE_BUILD_SHIPPING
