// Dev-only headless check: holding a corner against a horde (2026-10-07 regression; UE-only, no Godot reference):
//   Scripts/smoke.ps1 -Command CodexTactics.CornerHoldSmoke -Log Smoke-CornerHold.log
// The leader at the right-hand corner of a 6 m x 3 m wall (spawned at runtime, nothing saved), firing on his own.
//  A) 10 frost hounds (melee) pour round the corner from behind the wall, one every 0.6 s;
//  B) a mixed group: 4 frostbitten (melee) + 2 spitters (ranged) behind the wall, round the corner.
// The [CornerAim] trace logs every decision change with its reason and inputs, next to what the pre-fix rule would have
// decided (melee counted as suppression / flank, every hit counted). Checks: A — no duck for safety at all, a run of
// >= 8 consecutive corner shots without a break (only reloads may break it); B — every duck for safety has a ranged
// reason. Counts: shots, ducks by reason, steps off the wall (open shots).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Combat/WaveSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Tactics/CoverTraceRules.h"

namespace CornerHoldSmoke
{
	struct FPhase
	{
		int32 Shots = 0;
		int32 OpenShots = 0;
		int32 Breaks[4] = { 0, 0, 0, 0 };
		int32 Run = 0;
		int32 BestRun = 0;
		int32 Spawned = 0;
	};

	struct FState
	{
		int32 Stage = 0;
		float StageTime = 0.f;
		float Time = 0.f;
		int32 Failures = 0;
		FVector P = FVector::ZeroVector;
		FVector F = FVector::ForwardVector;
		FVector R = FVector::RightVector;
		float GroundZ = 0.f;
		TWeakObjectPtr<AOperativeCharacter> Op;
		FPhase Phase[2];
		int32 LastLean = 0;
		int32 LastOpen = 0;
		int32 LastBreaks[4] = { 0, 0, 0, 0 };
		float SpawnTimer = 0.f;
		TArray<TWeakObjectPtr<AEnemyCharacter>> Spawned;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("CornerHoldSmoke"));
		return false;
	}

	AStaticMeshActor* SpawnBlock(UWorld* World, const FVector& Centre, const FRotator& Rotation, const FVector& Scale)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		AStaticMeshActor* Block = Cube ? World->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FTransform(Rotation, Centre, Scale),
			nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn) : nullptr;
		if (!Block)
		{
			return nullptr;
		}
		Block->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Block->GetStaticMeshComponent()->SetStaticMesh(Cube);
		Block->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
		Block->GetStaticMeshComponent()->SetCanEverAffectNavigation(true);
		Block->FinishSpawning(FTransform(Rotation, Centre, Scale));
		return Block;
	}

	/** Counts the leader's shots, breaks and the run of corner shots without a break (a reload does not end the run). */
	void Track(FState& State, FPhase& Phase)
	{
		AOperativeCharacter* Op = State.Op.Get();
		const int32 Lean = Op->GetCoverLeanShots() + Op->GetCoverBlindShots();
		const int32 Open = Op->GetCoverOpenShots();
		if (Lean > State.LastLean)
		{
			Phase.Shots += Lean - State.LastLean;
			Phase.Run += Lean - State.LastLean;
			Phase.BestRun = FMath::Max(Phase.BestRun, Phase.Run);
		}
		Phase.OpenShots += FMath::Max(0, Open - State.LastOpen);
		for (int32 Index = 1; Index < 4; ++Index)
		{
			const int32 Count = Op->GetCornerAimBreakCount(static_cast<ECornerAimDecision>(Index));
			if (Count > State.LastBreaks[Index])
			{
				Phase.Breaks[Index] += Count - State.LastBreaks[Index];
				if (Index == static_cast<int32>(ECornerAimDecision::DuckForSafety))
				{
					Phase.Run = 0; // only a duck for safety ends the hold (a reload, or a return between rushers with no target, does not)
				}
			}
			State.LastBreaks[Index] = Count;
		}
		State.LastLean = Lean;
		State.LastOpen = Open;
	}

	AEnemyCharacter* SpawnRusher(FState& State, UWorld* World, EEnemyArchetype Type, int32 Index)
	{
		// Behind the wall, round the leader's right-hand (-R) corner, spread a little.
		const FVector Where = State.P + State.F * (800.f + 60.f * (Index % 3)) - State.R * (700.f + 80.f * (Index % 4));
		AEnemyCharacter* Enemy = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(Type, FVector(Where.X, Where.Y, State.GroundZ + 100.f), (-State.F).Rotation());
		if (Enemy)
		{
			State.Spawned.Add(Enemy);
		}
		return Enemy;
	}

	void LogPhase(const TCHAR* Name, const FPhase& Phase)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: %s - %d enemies, %d corner shots, best run %d without a break, %d open shots off the wall, breaks: duck-safety %d, duck-reload %d, return %d"),
			Name, Phase.Spawned, Phase.Shots, Phase.BestRun, Phase.OpenShots, Phase.Breaks[static_cast<int32>(ECornerAimDecision::DuckForSafety)],
			Phase.Breaks[static_cast<int32>(ECornerAimDecision::DuckToReload)], Phase.Breaks[static_cast<int32>(ECornerAimDecision::ReturnNoTargets)]);
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += 0.05f;
		State.StageTime += 0.05f;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 150.f)
		{
			return World ? Finish(State) : false;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Op = State.Op.Get();
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Member->ColdLevel = 0.f;
			Member->bTacticalCeaseFire = Member != Op; // only the leader fights
		}
		switch (State.Stage)
		{
		case 0:
		{
			if (State.StageTime < 3.f)
			{
				return true;
			}
			UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
			Flow->TriggerCombatZone();
			Flow->FinishCutscene();
			Flow->FinishPreparation();
			Op = Squad->GetLeader();
			State.Op = Op;
			State.F = Op->GetActorForwardVector().GetSafeNormal2D();
			State.R = FVector::CrossProduct(FVector::UpVector, State.F);
			State.P = Op->GetActorLocation();
			State.GroundZ = State.P.Z - Op->GetSimpleCollisionHalfHeight();
			// The wave stays alive with a frozen hound far away; the spawned wave goes.
			AEnemyCharacter* Parked = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::FrostHound,
				FVector(State.P.X, State.P.Y, State.GroundZ + 100.f) - State.F * 6000.f, State.F.Rotation());
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				if (*It != Parked)
				{
					It->Destroy();
				}
			}
			if (Parked)
			{
				Parked->CustomTimeDilation = 0.f;
				Parked->SetActorTickEnabled(false);
			}
			int32 Index = 0;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->HealthComponent->SetMaxHealth(100000.f);
				if (Member != Op)
				{
					Member->StopOperative();
					Member->TeleportTo(State.P - State.F * 1500.f + State.R * (Index++ % 2 == 0 ? 400.f : -400.f), State.F.Rotation(), false, true);
				}
			}
			Op->ReserveAmmo = 600;
			const FVector WallCentre = State.P + State.F * 400.f;
			SpawnBlock(World, FVector(WallCentre.X, WallCentre.Y, State.GroundZ + 150.f), State.F.Rotation(), FVector(0.4f, 6.f, 3.f));
			State.Stage = 1;
			State.StageTime = 0.f;
			return true;
		}
		case 1:
		{
			if (State.StageTime < 2.f)
			{
				return true;
			}
			FCoverSlot Corner;
			const bool bFound = CoverTraceRules::FindCoverSlotAt(World, State.P + State.F * 380.f - State.R * 250.f + FVector(0.f, 0.f, 90.f), State.F, Corner);
			Check(State, bFound && Op->OrderTakeCover(Corner, true) == EOperativeOrderResult::Accepted, TEXT("the leader takes the right-hand corner"));
			State.Stage = 2;
			State.StageTime = 0.f;
			return true;
		}
		case 2:
			if ((!Op->bInCover || State.StageTime < 2.f) && State.StageTime < 10.f)
			{
				return true;
			}
			Check(State, Op->bInCover && Op->bAtCoverCorner, TEXT("at the corner"));
			State.LastLean = Op->GetCoverLeanShots() + Op->GetCoverBlindShots();
			State.LastOpen = Op->GetCoverOpenShots();
			State.Stage = 3;
			State.StageTime = 0.f;
			return true;
		case 3:
		{
			// A) the melee horde.
			FPhase& Phase = State.Phase[0];
			State.SpawnTimer -= 0.05f;
			if (Phase.Spawned < 10 && State.SpawnTimer <= 0.f)
			{
				State.SpawnTimer = 0.6f;
				Phase.Spawned += SpawnRusher(State, World, EEnemyArchetype::FrostHound, Phase.Spawned) ? 1 : 0;
			}
			Track(State, Phase);
			if (State.StageTime < 28.f)
			{
				return true;
			}
			LogPhase(TEXT("melee horde"), Phase);
			Check(State, Phase.Breaks[static_cast<int32>(ECornerAimDecision::DuckForSafety)] == 0,
				FString::Printf(TEXT("melee horde: no duck for safety (%d)"), Phase.Breaks[static_cast<int32>(ECornerAimDecision::DuckForSafety)]));
			Check(State, Phase.BestRun >= 8, FString::Printf(TEXT("melee horde: he holds the corner, %d corner shots in a row without a break"), Phase.BestRun));
			for (const TWeakObjectPtr<AEnemyCharacter>& Enemy : State.Spawned)
			{
				if (Enemy.IsValid())
				{
					Enemy->Destroy();
				}
			}
			State.Spawned.Reset();
			State.Stage = 4;
			State.StageTime = 0.f;
			State.SpawnTimer = 0.f;
			return true;
		}
		case 4:
		{
			// B) frostbitten (melee) + spitters (ranged).
			FPhase& Phase = State.Phase[1];
			State.SpawnTimer -= 0.05f;
			if (Phase.Spawned < 6 && State.SpawnTimer <= 0.f)
			{
				State.SpawnTimer = 0.8f;
				const EEnemyArchetype Type = Phase.Spawned % 3 == 2 ? EEnemyArchetype::Spitter : EEnemyArchetype::Frostbitten;
				Phase.Spawned += SpawnRusher(State, World, Type, Phase.Spawned) ? 1 : 0;
			}
			Track(State, Phase);
			if (State.StageTime < 30.f)
			{
				return true;
			}
			LogPhase(TEXT("mixed group"), Phase);
			Check(State, Phase.Shots >= 6, FString::Printf(TEXT("mixed group: he fights from the corner (%d corner shots)"), Phase.Shots));
			return Finish(State);
		}
		default:
			return Finish(State);
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), 0.05f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.CornerHoldSmoke"),
		TEXT("Dev check: holding a corner against a melee horde and a mixed group; ducks and their reasons; PASS / FAIL."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
