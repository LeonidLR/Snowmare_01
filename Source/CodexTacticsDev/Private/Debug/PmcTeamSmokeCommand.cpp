// Dev-only check of the operative teams (user request 2026-11-03, docs/systems/pmc-squad-ai.md) on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.PmcTeamSmoke
// A hostile PMC fighter (the operative body on TeamId 1) spawned 12 m in front of the squad on an empty arena: he is no squad member
// (not in USquadSubsystem: no selection / HUD / formation), carries the "Enemy" tag and sits in the enemy registry; the squad sees him and
// he sees the squad by the team sight; the leader's best target is him and his is a squad member; his shot hurts the leader and the
// leader's shot hurts him; a 3 m wall between them hides him from the squad (no target, not visible) and the squad from him; a hound
// spawned next to him hunts him (monsters attack the PMC) and he takes the hound as a self-defence target.

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

namespace PmcTeamSmoke
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
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("PmcTeamSmoke"));
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
			const TArray<AOperativeCharacter*> Members = Squad->GetMembers();
			TArray<const AActor*> Ignore;
			for (const AOperativeCharacter* Member : Members)
			{
				Ignore.Add(Member);
			}
			const SmokeUtils::FClearArena Arena = SmokeUtils::FindClearArena(World, Members[0]->GetActorLocation(), Members[0]->GetActorForwardVector(),
				FBox2D(FVector2D(-400.f, -700.f), FVector2D(2200.f, 700.f)), Ignore);
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
			State.Stage = 1;
			State.StageTime = 0.f;
			return true;
		}
		case 1:
		{
			if (State.StageTime < 1.f)
			{
				return true;
			}
			AOperativeCharacter* Hostile = State.Hostile.Get();
			AOperativeCharacter* Leader = State.Leader.Get();
			if (!Hostile || !Leader)
			{
				return Finish(State, false);
			}
			Pmc->RefreshSight();
			Check(State, Hostile->TeamId == 1 && !Hostile->IsPlayerTeam(), TEXT("the fighter is on team 1"));
			Check(State, !Squad->GetMembers().Contains(Hostile) && Squad->GetMembers().Num() == 3, TEXT("he is NOT a member of the player's squad (no selection / HUD / formation)"));
			Check(State, !Squad->GetSelectedGroup().Contains(Hostile), TEXT("he is not in the selection"));
			Check(State, Hostile->ActorHasTag(CombatQueries::EnemyTag), TEXT("he carries the \"Enemy\" tag"));
			Check(State, UEnemyRegistrySubsystem::GetTaggedEnemies(World).Contains(Hostile), TEXT("he is in the enemy registry"));
			Check(State, Pmc->GetTeamMembers(1).Contains(Hostile), TEXT("he is a member of the hostile team 1"));
			Check(State, Hostile->HealthComponent && Hostile->HealthComponent->IsAlive() && Hostile->CurrentWeapon != nullptr, TEXT("alive with a weapon in hands"));
			// The PMC body on the operative Animation Blueprint (user decision 2026-11-03): the mesh asset of pmc_operators.json and an operative
			// anim instance running on it (it only spawns when the skeletons are compatible).
			{
				const USkeletalMesh* Body = Hostile->GetMesh() ? Hostile->GetMesh()->GetSkeletalMeshAsset() : nullptr;
				const FString Expected = FPaths::GetBaseFilename(PmcOperatorRules::Get().BodyMesh);
				Check(State, Body && (Expected.IsEmpty() || Body->GetName() == Expected),
					FString::Printf(TEXT("he wears the PMC body (%s, expected %s)"), Body ? *Body->GetName() : TEXT("none"), *Expected));
				const UAnimInstance* Anim = Hostile->GetMesh() ? Hostile->GetMesh()->GetAnimInstance() : nullptr;
				Check(State, Anim && Anim->IsA<UOperativeAnimInstance>(), FString::Printf(TEXT("ABP_Operative drives him (anim instance %s)"), Anim ? *Anim->GetClass()->GetName() : TEXT("none")));
			}
			Check(State, Pmc->IsVisibleToTeam(0, Hostile) && Sight->IsVisibleToSquad(Hostile), TEXT("the squad sees him (team sight, IsVisibleToSquad)"));
			Check(State, Pmc->IsVisibleToTeam(1, Leader), TEXT("he sees the leader (team 1 sight)"));
			Check(State, Leader->FindBestCombatTarget() == Hostile, TEXT("the leader's best combat target is the fighter"));
			AActor* HisTarget = Hostile->FindBestCombatTarget();
			Check(State, HisTarget && HisTarget->IsA<AOperativeCharacter>() && Cast<AOperativeCharacter>(HisTarget)->IsPlayerTeam(),
				FString::Printf(TEXT("his best combat target is a squad member (%s)"), HisTarget ? *HisTarget->GetName() : TEXT("none")));
			Check(State, !Hostile->CanHitEnemy(Hostile) && !Leader->CanHitEnemy(Leader), TEXT("nobody targets himself"));
			// Damage both ways (forced hits).
			State.LeaderHp = Leader->HealthComponent->GetCurrentHealth();
			State.HostileHp = Hostile->HealthComponent->GetCurrentHealth();
			Hostile->bForceHitForTesting = true;
			Leader->bForceHitForTesting = true;
			const bool bHostileShot = Hostile->ShootAtTarget(Leader, 1.f);
			const bool bLeaderShot = Leader->ShootAtTarget(Hostile, 1.f);
			Hostile->bForceHitForTesting = false;
			Leader->bForceHitForTesting = false;
			Check(State, bHostileShot && Leader->HealthComponent->GetCurrentHealth() < State.LeaderHp,
				FString::Printf(TEXT("his shot hurts the leader (%.0f -> %.0f)"), State.LeaderHp, Leader->HealthComponent->GetCurrentHealth()));
			Check(State, bLeaderShot && Hostile->HealthComponent->GetCurrentHealth() < State.HostileHp,
				FString::Printf(TEXT("the leader's shot hurts him (%.0f -> %.0f)"), State.HostileHp, Hostile->HealthComponent->GetCurrentHealth()));
			// A 3 m wall between them.
			State.Wall = SpawnWall(World, State.Origin + State.F * 600.f + FVector(0.f, 0.f, 150.f), State.F.Rotation(), FVector(0.3f, 8.f, 3.f));
			Check(State, State.Wall.IsValid(), TEXT("a 3 m wall spawned between them"));
			State.Stage = 2;
			State.StageTime = 0.f;
			return true;
		}
		case 2:
		{
			if (State.StageTime < 2.5f) // the shots above demasked both for SightRules::DemaskSeconds (2 s)
			{
				return true;
			}
			AOperativeCharacter* Hostile = State.Hostile.Get();
			AOperativeCharacter* Leader = State.Leader.Get();
			if (!Hostile || !Leader)
			{
				return Finish(State, false);
			}
			Sight->Refresh();
			Pmc->RefreshSight();
			Check(State, !Pmc->IsVisibleToTeam(0, Hostile) && !Sight->IsVisibleToSquad(Hostile), TEXT("behind the wall the squad does not see him"));
			Check(State, !Pmc->IsVisibleToTeam(1, Leader), TEXT("... and he does not see the squad"));
			Check(State, Leader->FindBestCombatTarget() == nullptr, TEXT("the leader has no target (not visible)"));
			Check(State, Hostile->FindBestCombatTarget() == nullptr, TEXT("he has no target either"));
			// A hound next to him: monsters attack the PMC, he defends himself.
			if (UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>())
			{
				State.Hound = Waves->SpawnEnemy(EEnemyArchetype::FrostHound, State.Origin + State.F * 1200.f + State.R * 500.f);
			}
			Check(State, State.Hound.IsValid(), TEXT("a hound spawned 5 m beside him"));
			State.Stage = 3;
			State.StageTime = 0.f;
			return true;
		}
		case 3:
		{
			AEnemyCharacter* Hound = State.Hound.Get();
			AOperativeCharacter* Hostile = State.Hostile.Get();
			if (!Hound || !Hostile)
			{
				return Finish(State, false);
			}
			if (State.StageTime < 8.f && Hound->GetCurrentTarget() != Hostile)
			{
				return true;
			}
			Check(State, Hound->GetCurrentTarget() == Hostile, FString::Printf(TEXT("the hound hunts the PMC fighter (target %s)"),
				Hound->GetCurrentTarget() ? *Hound->GetCurrentTarget()->GetName() : TEXT("none")));
			Pmc->RefreshSight();
			Check(State, Pmc->GetTargetsFor(*Hostile).Contains(Hound), TEXT("the fighter takes the hound as a self-defence target"));
			Check(State, Pmc->IsHostileTo(*Hostile, Hound), TEXT("the hound counts as his hostile"));
			SmokeUtils::ParkEnemyFarAway(Hound);
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
		TEXT("CodexTactics.PmcTeamSmoke"),
		TEXT("Dev check: a hostile PMC fighter (operative body, team 1) is no squad member, is seen / targeted / hurt by the squad and sees / targets / hurts it; a wall hides both; monsters attack him; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
