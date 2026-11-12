// Runtime regression for visual stealth detection and fixed-point PMC return fire.
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/PmcOperatorRules.h"
#include "AI/PmcSquadSubsystem.h"
#include "Animation/AnimInstance.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/Paths.h"
#include "Characters/SquadSubsystem.h"
#include "Characters/StealthModeRules.h"
#include "CodexTactics.h"
#include "Combat/EncounterQueries.h"
#include "Combat/EnemyRegistrySubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

namespace StealthDetectionSmoke
{
	constexpr float StepSeconds = 0.1f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		FVector Origin = FVector::ZeroVector;
		FVector F = FVector::ForwardVector;
		FVector R = FVector::RightVector;
		TWeakObjectPtr<AOperativeCharacter> Hostile;
		TWeakObjectPtr<AOperativeCharacter> Leader;
		TWeakObjectPtr<AStaticMeshActor> Wall;
		TWeakObjectPtr<AEnemyCharacter> Hound;
		float LeaderHp = 0.f;
		float HostileHp = 0.f;
		FVector KnownPoint = FVector::ZeroVector;
		TWeakObjectPtr<UMaterialInterface> Material;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("StealthDetectionSmoke"));
		return false;
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

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 90.f)
		{
			return World ? Finish(State, false) : false;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World);
		UTacticalSightSubsystem* Sight = World->GetSubsystem<UTacticalSightSubsystem>();
		switch (State.Stage)
		{
		case 0:
		{
			if (State.StageTime < 2.f)
			{
				return true;
			}
			if (!Squad || !Pmc || !Sight || Squad->GetMembers().Num() < 3)
			{
				Check(State, false, TEXT("squad of 3, UPmcSquadSubsystem and the sight subsystem present"));
				return Finish(State, false);
			}
			// Keep the range comparison within a verified clear test arena.
			for (const TCHAR* Name : { TEXT("Codex.Sight.LongRange"), TEXT("Codex.Sight.Conditions"), TEXT("Codex.Sight.HeightRange") })
			{
				if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name)) { Var->Set(0); }
			}
			const TArray<AOperativeCharacter*> Members = Squad->GetMembers();
			TArray<const AActor*> Ignore;
			for (const AOperativeCharacter* Member : Members)
			{
				Ignore.Add(Member);
			}
			const SmokeUtils::FClearArena Arena = SmokeUtils::FindClearArena(World, Members[0]->GetActorLocation(), Members[0]->GetActorForwardVector(),
				FBox2D(FVector2D(-1800.f, -700.f), FVector2D(2200.f, 700.f)), Ignore);
			Check(State, Arena.bFound, FString::Printf(TEXT("an empty arena found (tried %d)"), Arena.Tried));
			if (!Arena.bFound)
			{
				return Finish(State, false);
			}
			State.Origin = Arena.Origin;
			State.F = Arena.Forward;
			State.R = Arena.Right;
			Squad->SetFollowersHolding(true);
			for (int32 Index = 0; Index < Members.Num(); ++Index)
			{
				Members[Index]->bTacticalCeaseFire = true;
				Members[Index]->StopOperative();
				const FVector Spot = SmokeUtils::FreeSpot(World, State.Origin - State.F * (Index * 200.f) + State.R * ((Index % 2) * 150.f), Members[Index]);
				Members[Index]->TeleportTo(Spot, State.F.Rotation(), false, true);
			}
			State.Leader = Members[0];
			const TArray<AOperativeCharacter*> Spawned = Pmc->SpawnSquad(1, State.Origin + State.F * 1200.f, (-State.F).Rotation(), 1, 5);
			Check(State, Spawned.Num() == 1 && Spawned[0] != nullptr, TEXT("one hostile PMC fighter spawned 12 m ahead"));
			if (Spawned.IsEmpty() || !Spawned[0])
			{
				return Finish(State, false);
			}
			State.Hostile = Spawned[0];
			Spawned[0]->bTacticalCeaseFire = true;
			State.Stage = 1;
			State.StageTime = 0.f;
			return true;
		}
		case 1:
		{
			if (State.StageTime < 1.f) { return true; }
			AOperativeCharacter* Hostile = State.Hostile.Get();
			AOperativeCharacter* Leader = State.Leader.Get();
			if (!Hostile || !Leader) { return Finish(State, false); }
			// Compare the same clear target at a distance between the normal and stealth sight ranges.
			const FVector Original = Leader->GetActorLocation();
			const float Range = Sight->GetSightRangeCm(*Hostile);
			Leader->TeleportTo(Hostile->GetActorLocation() - State.F * (Range * 0.45f), Leader->GetActorRotation(), false, true);
			Hostile->SetActorRotation((-State.F).Rotation());
			Check(State, Sight->CanOperativeSee(*Hostile, UTacticalSightSubsystem::ProfilePoint(*Leader)), TEXT("normal-range fixture has field of view and clear sight"));
			Pmc->RefreshSight();
			Check(State, Pmc->IsVisibleToTeam(1, Leader), TEXT("normal target visible at comparison distance"));
			Check(State, Leader->SetStealthMode(true), TEXT("stealth enabled at range"));
			Pmc->RefreshSight();
			Check(State, !Pmc->IsVisibleToTeam(1, Leader) && Leader->IsInStealthMode(), TEXT("same target beyond reduced range stays hidden"));
			Leader->SetStealthMode(false);
			Leader->TeleportTo(Original, Leader->GetActorRotation(), false, true);
			// Turn the observer away to enter stealth safely, then face the real target.
			Hostile->SetActorRotation(State.F.Rotation());
			State.Material = Leader->GetMesh()->GetMaterial(0);
			Check(State, Leader->SetStealthMode(true), TEXT("stealth enabled with material"));
			Check(State, Leader->GetMesh()->GetMaterial(0) != State.Material.Get(), TEXT("stealth material applied"));
			Hostile->SetActorRotation((-State.F).Rotation());
			Pmc->RefreshSight();
			Check(State, Pmc->IsVisibleToTeam(1, Leader), TEXT("PMC sees target inside stealth range"));
			Check(State, !Leader->IsInStealthMode() && Leader->GetMesh()->GetMaterial(0) == State.Material.Get(), TEXT("visual confirmation exits stealth and restores material"));
			Check(State, Pmc->GetTeamBelief(1, Leader, State.KnownPoint), TEXT("visual contact recorded"));
			Hostile->bTacticalCeaseFire = false;
			const int32 RoundsBefore = Hostile->GetRoundsSpent();
			Hostile->ShootAtTarget(Leader, 1.f); // return value is hit/miss, not whether a round was fired
			Check(State, Hostile->GetRoundsSpent() > RoundsBefore && Hostile->GetLastShotTime() >= 0.0, TEXT("PMC fired at the visible target"));
			State.Wall = SpawnWall(World, State.Origin - State.F * 250.f + FVector(0.f, 0.f, 150.f), State.F.Rotation(), FVector(0.3f, 8.f, 3.f));
			Check(State, State.Wall.IsValid(), TEXT("sight blocking wall spawned"));
			Leader->TeleportTo(State.Origin - State.F * 500.f, Leader->GetActorRotation(), false, true);
			Pmc->RefreshSight();
			Check(State, !Pmc->IsVisibleToTeam(1, Leader), TEXT("wall blocks sight"));
			Check(State, Leader->SetStealthMode(true), TEXT("hidden target enters stealth"));
			Leader->TeleportTo(Leader->GetActorLocation() + State.R * 100.f, Leader->GetActorRotation(), false, true);
			Pmc->RefreshSight();
			FVector Belief;
			Check(State, Pmc->GetTeamBelief(1, Leader, Belief) && Belief.Equals(State.KnownPoint, 1.f), TEXT("hidden movement does not move last known point"));
			Check(State, Leader->IsInStealthMode(), TEXT("blocked sight does not cancel stealth"));
			Check(State, Hostile->HasSuppressOrder() && !Hostile->GetSuppressTarget() && Hostile->GetSuppressPoint().Equals(State.KnownPoint, 1.f), TEXT("automatic fire continues at fixed point, without an actor target"));
			State.HostileHp = static_cast<float>(Hostile->GetSuppressRoundsFired());
			State.Stage = 2;
			State.StageTime = 0.f;
			return true;
		}
		case 2:
		{
			AOperativeCharacter* Hostile = State.Hostile.Get();
			if (!Hostile) { return Finish(State, false); }
			if (State.StageTime < 2.f) { return true; }
			Check(State, Hostile->GetSuppressRoundsFired() > State.HostileHp, TEXT("fixed-point suppression fires rounds"));
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
		TEXT("CodexTactics.StealthDetectionSmoke"),
		TEXT("Checks visual stealth cancellation, material restoration, blocked sight and fixed PMC contact memory."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
