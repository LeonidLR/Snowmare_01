// Dev-only headless check of the game designer's Demo 01 report 2026-10-22 (screenshot: hounds outside the lake perimeter "afraid
// of the heat" of the running generator although two walls separate them), on L_MovementTest in a runtime arena:
//   Scripts/smoke_parallel.ps1 -Commands HeatFearWallSmoke
//  1. an active heat source (a running generator's warm zone, 6 m) and two 3 m walls 1.5 m / 2.5 m past it: a hound 3.5 m from the
//     heat BEHIND both walls never fears it (no flight, no "FEAR OF FIRE!");
//  2. a hound 3.5 m from the same heat in the OPEN fears it at once (flees);
//  3. a burning-barrel-style source behind one wall: a frostbitten behind it - no fear; then the wall is removed - fear.
// All enemy types share the rule (AEnemyCharacter::FindNearestFireZone / IsInFearZone -> EnemyAIRules::FeelsHeat).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/HeatSourceComponent.h"
#include "Kismet/GameplayStatics.h"

namespace HeatFearWallSmoke
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
		float GroundZ = 0.f;
		TWeakObjectPtr<AEnemyCharacter> Walled;
		TWeakObjectPtr<AEnemyCharacter> Open;
		TWeakObjectPtr<AEnemyCharacter> Third;
		TWeakObjectPtr<AStaticMeshActor> ThirdWall;
		bool bWalledFeared = false;
		bool bOpenFeared = false;
		bool bThirdFeared = false;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("HeatFearWallSmoke"));
		return false;
	}

	void Next(FState& State, int32 Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
	}

	FVector At(const FState& State, float Along, float Side, float Up = 0.f)
	{
		const FVector P = State.O + State.F * Along + State.R * Side;
		return FVector(P.X, P.Y, State.GroundZ + Up);
	}

	AStaticMeshActor* SpawnWall(UWorld* World, const FState& State, float Along, float Side, float Length)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		if (!Cube)
		{
			return nullptr;
		}
		const FTransform Transform(FRotator(0.f, State.F.Rotation().Yaw, 0.f), At(State, Along, Side, 150.f), FVector(0.3f, Length / 100.f, 3.f));
		AStaticMeshActor* Block = World->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
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

	/** A running generator's warm zone at Where (an actor whose root is the heat source). */
	void SpawnHeat(UWorld* World, const FVector& Where, float Radius)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Owner = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Where), Params);
		if (!Owner)
		{
			return;
		}
		UHeatSourceComponent* Heat = NewObject<UHeatSourceComponent>(Owner, TEXT("SmokeHeat"));
		Owner->SetRootComponent(Heat);
		Heat->RegisterComponent();
		Heat->SetWorldLocation(Where);
		Heat->Radius = Radius;
		Heat->SetHeatActive(true);
	}

	/** A hound / frostbitten that stays where it is put (its AI runs: the fear check, the flight order - but it cannot walk). */
	AEnemyCharacter* SpawnHeld(UWorld* World, EEnemyArchetype Type, const FVector& Where)
	{
		UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
		AEnemyCharacter* Enemy = Waves ? Waves->SpawnEnemy(Type, Where + FVector(0.f, 0.f, 100.f)) : nullptr;
		if (Enemy)
		{
			Enemy->GetCharacterMovement()->DisableMovement();
			Enemy->GetHealthComponent()->SetMaxHealth(100000.f, false);
			Enemy->GetHealthComponent()->Heal(100000.f);
		}
		return Enemy;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 60.f)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke stopped in stage %d"), State.Stage);
			return World ? Finish(State, false) : false;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		switch (State.Stage)
		{
		case 0:
		{
			if (State.StageTime < 3.f || !Squad || !Squad->GetLeader())
			{
				return State.Time < 20.f || Finish(State, false);
			}
			Squad->SetSquadPosture(ESquadFirePosture::Passive);
			Squad->SetFollowersHolding(true);
			AOperativeCharacter* Leader = Squad->GetLeader();
			TArray<const AActor*> Ignore;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Ignore.Add(Member);
				Member->bTacticalCeaseFire = true;
			}
			if (APawn* Pawn = UGameplayStatics::GetPlayerPawn(World, 0))
			{
				Ignore.Add(Pawn);
			}
			const FVector Feet = Leader->GetActorLocation() - FVector(0.f, 0.f, Leader->GetSimpleCollisionHalfHeight());
			const SmokeUtils::FClearArena Arena = SmokeUtils::FindClearArena(World, Feet, Leader->GetActorForwardVector(),
				FBox2D(FVector2D(-200.f, -900.f), FVector2D(2600.f, 900.f)), Ignore);
			if (!Arena.bFound)
			{
				Check(State, false, TEXT("an empty, flat 28 x 18 m spot for the smoke's own set-up"));
				return Finish(State, false);
			}
			State.O = Arena.Origin;
			State.F = Arena.Forward;
			State.R = Arena.Right;
			FHitResult Ground;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(HeatFearWallGround), false);
			State.GroundZ = World->LineTraceSingleByChannel(Ground, State.O + FVector(0.f, 0.f, 200.f), State.O - FVector(0.f, 0.f, 400.f), ECC_Visibility, Params)
				? Ground.ImpactPoint.Z : State.O.Z;
			// The squad stays back at the arena origin; the heat 15 m ahead, two walls 1.5 / 2.5 m past it.
			SpawnHeat(World, At(State, 1500.f, 0.f, 60.f), 600.f);
			SpawnWall(World, State, 1650.f, 0.f, 600.f);
			SpawnWall(World, State, 1750.f, 0.f, 600.f);
			State.Walled = SpawnHeld(World, EEnemyArchetype::FrostHound, At(State, 1850.f, 0.f));
			Check(State, State.Walled.IsValid() && State.Walled->DoesFearFire(), TEXT("a hound 3.5 m from the running heat, two walls in between"));
			Next(State, 1);
			return true;
		}
		case 1:
		{
			AEnemyCharacter* Walled = State.Walled.Get();
			State.bWalledFeared |= Walled && Walled->IsFleeingFire();
			if (State.StageTime < 2.f)
			{
				return true;
			}
			Check(State, Walled && !State.bWalledFeared && FVector::Dist2D(Walled->GetActorLocation(), At(State, 1500.f, 0.f)) < 600.f,
				FString::Printf(TEXT("behind two walls, %.1f m from the heat: no fear in 2 s"),
					Walled ? FVector::Dist2D(Walled->GetActorLocation(), At(State, 1500.f, 0.f)) / 100.f : -1.f));
			State.Open = SpawnHeld(World, EEnemyArchetype::FrostHound, At(State, 1500.f, -350.f) + State.F * -50.f);
			Next(State, 2);
			return true;
		}
		case 2:
		{
			AEnemyCharacter* Open = State.Open.Get();
			State.bOpenFeared |= Open && Open->IsFleeingFire();
			State.bWalledFeared |= State.Walled.IsValid() && State.Walled->IsFleeingFire();
			if (!State.bOpenFeared && State.StageTime < 2.f)
			{
				return true;
			}
			Check(State, State.bOpenFeared, FString::Printf(TEXT("a hound 3.5 m from the same heat in the open fears it (%.2f s)"), State.StageTime));
			Check(State, !State.bWalledFeared, TEXT("... while the walled one still does not"));
			// 3. a frostbitten behind one wall, 3 m from a second heat; then the wall goes.
			SpawnHeat(World, At(State, 500.f, 600.f, 60.f), 600.f);
			State.ThirdWall = SpawnWall(World, State, 650.f, 600.f, 500.f);
			State.Third = SpawnHeld(World, EEnemyArchetype::Frostbitten, At(State, 800.f, 600.f));
			Next(State, 3);
			return true;
		}
		case 3:
		{
			AEnemyCharacter* Third = State.Third.Get();
			State.bThirdFeared |= Third && Third->IsFleeingFire();
			if (State.StageTime < 2.f)
			{
				return true;
			}
			Check(State, Third && Third->DoesFearFire() && !State.bThirdFeared, TEXT("a frostbitten 3 m from a heat behind a wall: no fear"));
			if (AStaticMeshActor* Wall = State.ThirdWall.Get())
			{
				Wall->Destroy();
			}
			Next(State, 4);
			return true;
		}
		case 4:
		{
			AEnemyCharacter* Third = State.Third.Get();
			State.bThirdFeared |= Third && Third->IsFleeingFire();
			if (!State.bThirdFeared && State.StageTime < 2.f)
			{
				return true;
			}
			Check(State, State.bThirdFeared, FString::Printf(TEXT("the wall gone: the frostbitten fears the heat (%.2f s)"), State.StageTime));
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
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.HeatFearWallSmoke"),
		TEXT("Dev check (designer report 2026-10-22): enemies fear a heat source only with a clear line to it; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
