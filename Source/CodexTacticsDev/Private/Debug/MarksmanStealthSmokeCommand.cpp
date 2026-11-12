// Runtime regression for marksman stealth sight and remembered-point fire.
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/MarksmanEnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/SightRules.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Combat/WaveSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Survival/ColdSurvivalComponent.h"
#include "TimerManager.h"

namespace MarksmanStealthSmoke
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
		TWeakObjectPtr<AMarksmanEnemyCharacter> Hostile;
		TWeakObjectPtr<AStaticMeshActor> Blocker;
		int32 ShotsBefore = 0;
		TWeakObjectPtr<AOperativeCharacter> Leader;
		TWeakObjectPtr<AStaticMeshActor> Wall;
		float LeaderHp = 0.f;
		double HiddenStartTime = 0.0;
		FVector KnownPoint = FVector::ZeroVector;
		FVector LeaderSpot = FVector::ZeroVector;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("MarksmanStealthSmoke"));
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
		if (!World || State.Time > 90.f) { return World ? Finish(State, false) : false; }
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UTacticalSightSubsystem* Sight = World->GetSubsystem<UTacticalSightSubsystem>();
		AMarksmanEnemyCharacter* Marksman = State.Hostile.Get();
		AOperativeCharacter* Leader = State.Leader.Get();
		// The movement map's scripted horde can otherwise pounce the fixture during the memory timer.
		for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
		{
			if (*It != Marksman && It->CustomTimeDilation > 0.f) { SmokeUtils::ParkEnemyFarAway(*It); }
		}
		switch (State.Stage)
		{
		case 0:
		{
			if (State.StageTime < 2.f) { return true; }
			if (!Squad || !Sight || Squad->GetMembers().Num() < 3 || !World->GetSubsystem<UWaveSubsystem>())
			{
				Check(State, false, TEXT("squad, wave and sight subsystems available")); return Finish(State, false);
			}
			for (const TCHAR* Name : { TEXT("Codex.Sight.LongRange"), TEXT("Codex.Sight.Conditions"), TEXT("Codex.Sight.HeightRange") })
			{
				if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name)) { Var->Set(0); }
			}
			const TArray<AOperativeCharacter*> Members = Squad->GetMembers();
			TArray<const AActor*> Ignore;
			for (const AOperativeCharacter* Member : Members) { Ignore.Add(Member); }
			const SmokeUtils::FClearArena Arena = SmokeUtils::FindClearArena(World, Members[0]->GetActorLocation(), Members[0]->GetActorForwardVector(),
				FBox2D(FVector2D(-1800.f, -700.f), FVector2D(2200.f, 700.f)), Ignore);
			Check(State, Arena.bFound, TEXT("clear range test arena"));
			if (!Arena.bFound) { return Finish(State, false); }
			State.Origin = Arena.Origin; State.F = Arena.Forward; State.R = Arena.Right;
			Squad->SetFollowersHolding(true);
			for (int32 Index = 0; Index < Members.Num(); ++Index)
			{
				Members[Index]->bTacticalCeaseFire = true;
				Members[Index]->HealthComponent->SetMaxHealth(100000.f); // restores current HP by default
				Members[Index]->ColdLevel = 0.f;
				if (Members[Index]->ColdSurvival) { Members[Index]->ColdSurvival->SetComponentTickEnabled(false); }
				Members[Index]->StopOperative();
				const FVector Point = Index == 0 ? SmokeUtils::FreeSpot(World, State.Origin, Members[Index]) : State.Origin + State.R * (6000.f + Index * 300.f);
				Members[Index]->TeleportTo(Point, State.F.Rotation(), false, true);
				if (Index > 0)
				{
					Members[Index]->GetCharacterMovement()->DisableMovement();
					Members[Index]->SetStealthMode(true);
				}
			}
			State.Leader = Members[0];
			State.LeaderSpot = Members[0]->GetActorLocation();
			SmokeUtils::StartWaveFight(World->GetSubsystem<UGameFlowSubsystem>());
			Marksman = Cast<AMarksmanEnemyCharacter>(World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::Marksman,
				State.Origin + State.F * 1200.f, (-State.F).Rotation()));
			Check(State, Marksman != nullptr, TEXT("marksman spawned"));
			if (!Marksman) { return Finish(State, false); }
			State.Hostile = Marksman;
			Marksman->bKnowsSquadPosition = true; // even omniscient hordes must obey stealth visibility
			Marksman->MarksmanConfig.PreferredMinRange = 0.f;
			Marksman->MarksmanConfig.PreferredMaxRange = 5000.f;
			Marksman->MarksmanConfig.AimDuration = 1.f;
			Marksman->MarksmanConfig.ShotCooldown = 0.5f;
			Marksman->Alert();
			Leader = State.Leader.Get();
			const float Range = Sight->GetSightRangeCm(*Marksman);
			Leader->TeleportTo(Marksman->GetActorLocation() - State.F * (Range * 0.45f), Leader->GetActorRotation(), false, true);
			Check(State, Marksman->HasLineOfFireTo(Leader), TEXT("normal target has clear firing line at comparison distance"));
			State.Stage = 7; State.StageTime = 0.f; return true;
		}
		case 7:
		{
			if (!Marksman || !Leader) { return Finish(State, false); }
			if (State.StageTime < 0.5f) { return true; }
			Check(State, Marksman->GetCurrentTarget() == Leader, TEXT("normal target acquired at comparison distance"));
			Check(State, Leader->SetStealthMode(true), TEXT("target enters stealth at same distance"));
			State.Stage = 1; State.StageTime = 0.f; return true;
		}
		case 1:
		{
			if (!Marksman || !Leader) { return Finish(State, false); }
			if (State.StageTime < 1.f) { return true; }
			Check(State, Leader->IsInStealthMode() && Marksman->GetCurrentTarget() != Leader, TEXT("horde cannot acquire stealth target beyond reduced range"));
			Marksman->SetActorTickEnabled(false);
			Marksman->SetActorRotation(State.F.Rotation());
			Leader->TeleportTo(State.LeaderSpot, Leader->GetActorRotation(), false, true);
			Sight->Refresh();
			FVector Belief;
			bool bPerceived = true;
			const bool bHasBelief = Sight->GetBelief(Marksman, Leader, Belief, &bPerceived);
			Check(State, Leader->IsInStealthMode() && (!bHasBelief || !bPerceived), TEXT("behind-back stealth is not visually detected, even by horde belief"));
			Marksman->SetActorRotation((-State.F).Rotation());
			Marksman->SetActorTickEnabled(true);
			State.Stage = 2; State.StageTime = 0.f; return true;
		}
		case 2:
		{
			if (!Marksman || !Leader) { return Finish(State, false); }
			if (State.StageTime < 0.3f || (!Marksman->HasRememberedTarget() && State.StageTime < 5.f)) { return true; }
			Check(State, !Leader->IsInStealthMode(), TEXT("confirmed nearby vision cancels stealth"));
			Check(State, Marksman->HasRememberedTarget(), TEXT("marksman aimed at visible target"));
			State.KnownPoint = Marksman->GetRememberedAimPoint();
			State.Wall = SpawnWall(World, State.Origin - State.F * 250.f + FVector(0.f, 0.f, 150.f), State.F.Rotation(), FVector(0.3f, 8.f, 3.f));
			Check(State, State.Wall.IsValid(), TEXT("wall spawned behind observed point"));
			Leader->TeleportTo(State.LeaderSpot - State.F * 500.f + State.R * 100.f, Leader->GetActorRotation(), false, true);
			Leader->SetStealthMode(true);
			State.LeaderHp = Leader->HealthComponent->GetCurrentHealth();
			State.ShotsBefore = Marksman->GetShotsFired();
			State.Stage = 3; State.StageTime = 0.f; return true;
		}
		case 3:
		{
			if (!Marksman || !Leader) { return Finish(State, false); }
			if (State.StageTime < 3.f) { return true; }
			Check(State, Leader->IsInStealthMode(), TEXT("wall prevents reacquisition"));
			Check(State, Marksman->GetRememberedAimPoint().Equals(State.KnownPoint, 1.f), TEXT("hidden movement leaves aim point fixed"));
			Check(State, Marksman->GetShotsFired() > State.ShotsBefore, TEXT("marksman fires at remembered point"));
			Check(State, FMath::IsNearlyEqual(Leader->HealthComponent->GetCurrentHealth(), State.LeaderHp), TEXT("hidden actor takes no direct hit"));
			State.Blocker = SpawnWall(World, State.Origin + State.F * 600.f + FVector(0.f, 0.f, 150.f), State.F.Rotation(), FVector(0.3f, 8.f, 3.f));
			Check(State, State.Blocker.IsValid(), TEXT("wall blocks remembered point"));
			State.ShotsBefore = Marksman->GetShotsFired(); State.Stage = 4; State.StageTime = 0.f; return true;
		}
		case 4:
		{
			if (!Marksman || !Leader) { return Finish(State, false); }
			if (State.StageTime < 3.f) { return true; }
			Check(State, Marksman->GetShotsFired() == State.ShotsBefore, TEXT("blocked remembered point receives no rounds"));
			if (State.Blocker.IsValid()) { State.Blocker->Destroy(); }
			Leader->TeleportTo(State.LeaderSpot, Leader->GetActorRotation(), false, true);
			State.Stage = 5; State.StageTime = 0.f; return true;
		}
		case 5:
		{
			if (!Marksman || !Leader) { return Finish(State, false); }
			if (State.StageTime < 0.5f || (!Marksman->HasRememberedTarget() && State.StageTime < 5.f)) { return true; }
			Check(State, !Leader->IsInStealthMode(), TEXT("reacquisition cancels stealth"));
			Check(State, Marksman->HasRememberedTarget(), TEXT("fresh visible aim cached before expiry test"));
			Leader->TeleportTo(State.LeaderSpot - State.F * 500.f, Leader->GetActorRotation(), false, true);
			Check(State, Leader->SetStealthMode(true), TEXT("target hides for the memory expiry test"));
			State.HiddenStartTime = World->GetTimeSeconds();
			State.Stage = 6; State.StageTime = 0.f; return true;
		}
		case 6:
		{
			if (!Marksman || !Leader) { return Finish(State, false); }
			if (World->GetTimeSeconds() - State.HiddenStartTime < SightRules::ForgetSeconds + 1.f) { return true; }
			Check(State, Leader->IsInStealthMode(), TEXT("target remained hidden through memory expiry"));
			Check(State, !Marksman->HasRememberedTarget(), TEXT("remembered fire expires after world-time memory"));
			return Finish(State, true);
		}
		default: return Finish(State, false);
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
		TEXT("CodexTactics.MarksmanStealthSmoke"),
		TEXT("Checks marksman stealth sight, fixed-point fire, wall blocking, reacquisition and expiry."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
