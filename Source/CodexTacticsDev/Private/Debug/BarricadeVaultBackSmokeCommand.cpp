// Dev-only headless check of the game designer's Demo 01 report 2026-10-22 ("I moved the barricade and set it in one place; the
// operative vaulted over it, but cannot vault back and walks around"), on L_MovementTest in a runtime arena (nothing saved):
//   Scripts/smoke_parallel.ps1 -Commands BarricadeVaultBackSmoke
// Two long walls (6 m, 2.5 m high) with a 3.2 m gap between them, the leader on the near side, a goal on the far side.
//  1. MOVED barricade: a barricade put down 5 m aside is pushed into the gap by a squad mate (URelocationSubsystem::ExecuteRelocate,
//     the real push task). Set down, its box must stay OFF the navmesh (only its vault area moves with it - the bug: the box cut a
//     hole into the navmesh), the navmesh path across the gap stays short (no detour round the walls), and the leader vaults over
//     it to the far side AND back again;
//  2. PLACED barricade: a freshly placed one in the same gap - over and back the same way.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Components/BoxComponent.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/BarricadeActor.h"
#include "Interactables/RelocationSubsystem.h"
#include "Interactables/VaultNavigation.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "NavModifierComponent.h"

namespace BarricadeVaultBackSmoke
{
	constexpr float StepSeconds = 0.05f;
	/** Arena frame: the walls stand WallAlong cm ahead of the origin, the gap is GapHalf cm either side of the centre line. */
	constexpr float WallAlong = 600.f;
	constexpr float GapHalf = 160.f;
	constexpr float WallLength = 600.f;

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
		TWeakObjectPtr<AOperativeCharacter> Leader;
		TWeakObjectPtr<AOperativeCharacter> Worker;
		TWeakObjectPtr<ABarricadeActor> Barricade;
		/** Crossing bookkeeping. */
		FVector Goal = FVector::ZeroVector;
		bool bSawVault = false;
		int32 Crossing = 0;
		FString Case;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("BarricadeVaultBackSmoke"));
		return false;
	}

	void Next(FState& State, int32 Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
	}

	FVector At(const FState& State, float Along, float Side, float Up = 0.f)
	{
		return State.O + State.F * Along + State.R * Side + FVector(0.f, 0.f, Up);
	}

	float GapYaw(const FState& State)
	{
		return State.F.Rotation().Yaw + 90.f; // the barricade's 3 m run across the gap
	}

	void SpawnWall(UWorld* World, const FState& State, float Side)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		if (!Cube)
		{
			return;
		}
		const FVector Centre = FVector(At(State, WallAlong, Side).X, At(State, WallAlong, Side).Y, State.GroundZ + 125.f);
		const FTransform Transform(FRotator(0.f, State.F.Rotation().Yaw, 0.f), Centre, FVector(0.4f, WallLength / 100.f, 2.5f));
		AStaticMeshActor* Block = World->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Block)
		{
			return;
		}
		Block->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Block->GetStaticMeshComponent()->SetStaticMesh(Cube);
		Block->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
		Block->GetStaticMeshComponent()->SetCanEverAffectNavigation(true);
		Block->FinishSpawning(Transform);
	}

	ABarricadeActor* SpawnBarricade(UWorld* World, const FState& State, const FVector& Where)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<ABarricadeActor>(FVector(Where.X, Where.Y, State.GroundZ + 50.f), FRotator(0.f, GapYaw(State), 0.f), Params);
	}

	/** Navmesh path length from one side of the gap to the other (the squad's default query). */
	float PathLength(UWorld* World, const AOperativeCharacter* Querier, const FVector& From, const FVector& To)
	{
		const UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(World, From, To, const_cast<AOperativeCharacter*>(Querier));
		return Path && Path->IsValid() && !Path->IsPartial() ? Path->GetPathLength() : -1.f;
	}

	/** Orders the leader across the gap (to the far / near side) and starts watching the crossing. */
	void StartCrossing(FState& State, bool bToFar)
	{
		AOperativeCharacter* Leader = State.Leader.Get();
		State.Goal = bToFar ? At(State, WallAlong + 350.f, 0.f) : At(State, WallAlong - 350.f, 0.f);
		State.Goal.Z = Leader->GetActorLocation().Z;
		State.bSawVault = false;
		Check(State, Leader->OrderMoveTo(State.Goal, false) == EOperativeOrderResult::Accepted,
			FString::Printf(TEXT("%s: order %s accepted"), *State.Case, bToFar ? TEXT("over to the far side") : TEXT("back to the near side")));
	}

	/** True once the crossing is over (arrived or timed out); checks it. */
	bool WatchCrossing(FState& State, bool bToFar)
	{
		AOperativeCharacter* Leader = State.Leader.Get();
		State.bSawVault |= Leader->IsVaulting();
		const float Remaining = FVector::Dist2D(Leader->GetActorLocation(), State.Goal);
		if ((Remaining > 120.f || Leader->IsVaulting()) && State.StageTime < 9.f)
		{
			return false;
		}
		const float SideNow = FVector::DotProduct(Leader->GetActorLocation() - State.O, State.F);
		Check(State, State.bSawVault, FString::Printf(TEXT("%s: the leader VAULTED %s"), *State.Case, bToFar ? TEXT("over") : TEXT("back")));
		Check(State, Remaining <= 120.f && (bToFar ? SideNow > WallAlong + 100.f : SideNow < WallAlong - 100.f),
			FString::Printf(TEXT("%s: arrived on the %s side in %.1f s (%.0f cm off, never walked round the walls)"), *State.Case,
				bToFar ? TEXT("far") : TEXT("near"), State.StageTime, Remaining));
		return true;
	}

	/** Checks the barricade in the gap: box off the navmesh, its vault area there, a short path across. */
	void CheckGapNavigation(FState& State, UWorld* World)
	{
		ABarricadeActor* Barricade = State.Barricade.Get();
		const UNavModifierComponent* Area = VaultNavigation::FindVaultNavModifier(Barricade);
		Check(State, Barricade && FVector::Dist2D(Barricade->GetActorLocation(), At(State, WallAlong, 0.f)) < 40.f,
			FString::Printf(TEXT("%s: the barricade stands in the gap (%.0f cm off)"), *State.Case,
				Barricade ? FVector::Dist2D(Barricade->GetActorLocation(), At(State, WallAlong, 0.f)) : -1.f));
		Check(State, Barricade && Barricade->Box && !Barricade->Box->CanEverAffectNavigation() && Area && Area->CanEverAffectNavigation()
			&& VaultNavigation::IsVaultable(Barricade),
			FString::Printf(TEXT("%s: its box stays off the navmesh, the vault area on (box %d, area %d, vaultable %d)"), *State.Case,
				Barricade && Barricade->Box && Barricade->Box->CanEverAffectNavigation() ? 1 : 0, Area && Area->CanEverAffectNavigation() ? 1 : 0,
				VaultNavigation::IsVaultable(Barricade) ? 1 : 0));
		const FVector Near = At(State, WallAlong - 350.f, 0.f, 50.f);
		const FVector Far = At(State, WallAlong + 350.f, 0.f, 50.f);
		const float There = PathLength(World, State.Leader.Get(), Near, Far);
		const float Back = PathLength(World, State.Leader.Get(), Far, Near);
		Check(State, There > 0.f && There < 900.f && Back > 0.f && Back < 900.f,
			FString::Printf(TEXT("%s: navmesh path straight through the gap both ways (%.0f / %.0f cm for 700 cm; a detour round the walls is > 1500)"),
				*State.Case, There, Back));
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 150.f)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke stopped in stage %d"), State.Stage);
			return World ? Finish(State, false) : false;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		URelocationSubsystem* Relocation = World->GetSubsystem<URelocationSubsystem>();
		switch (State.Stage)
		{
		case 0:
		{
			if (State.StageTime < 3.f || !Squad || !Relocation || Squad->GetMembers().Num() < 2 || !Squad->GetLeader())
			{
				return State.Time < 20.f || Finish(State, false);
			}
			AOperativeCharacter* Leader = Squad->GetLeader();
			State.Leader = Leader;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->ColdLevel = 0.f;
				if (Member != Leader && !State.Worker.IsValid())
				{
					State.Worker = Member;
				}
			}
			Squad->SetFollowersHolding(true);
			TArray<const AActor*> Ignore;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Ignore.Add(Member);
			}
			if (APawn* Pawn = UGameplayStatics::GetPlayerPawn(World, 0))
			{
				Ignore.Add(Pawn);
			}
			const FVector Feet = Leader->GetActorLocation() - FVector(0.f, 0.f, Leader->GetSimpleCollisionHalfHeight());
			const SmokeUtils::FClearArena Arena = SmokeUtils::FindClearArena(World, Feet, Leader->GetActorForwardVector(),
				FBox2D(FVector2D(-200.f, -1000.f), FVector2D(1400.f, 1000.f)), Ignore);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: arena %s at %s facing %s (%d tried)"), Arena.bFound ? TEXT("found") : TEXT("NOT found"),
				*Arena.Origin.ToCompactString(), *Arena.Forward.ToCompactString(), Arena.Tried);
			if (!Arena.bFound)
			{
				Check(State, false, TEXT("an empty, flat, navigable 16 x 20 m spot for the smoke's own set-up"));
				return Finish(State, false);
			}
			State.O = Arena.Origin;
			State.F = Arena.Forward;
			State.R = Arena.Right;
			FHitResult Ground;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(BarricadeVaultBackGround), false);
			State.GroundZ = World->LineTraceSingleByChannel(Ground, State.O + FVector(0.f, 0.f, 200.f), State.O - FVector(0.f, 0.f, 400.f), ECC_Visibility, Params)
				? Ground.ImpactPoint.Z : State.O.Z;
			SpawnWall(World, State, -(GapHalf + WallLength * 0.5f));
			SpawnWall(World, State, GapHalf + WallLength * 0.5f);
			Leader->TeleportTo(FVector(At(State, WallAlong - 350.f, 0.f).X, At(State, WallAlong - 350.f, 0.f).Y, Leader->GetActorLocation().Z),
				State.F.Rotation(), false, true);
			AOperativeCharacter* Worker = State.Worker.Get();
			Worker->TeleportTo(FVector(At(State, 150.f, 500.f).X, At(State, 150.f, 500.f).Y, Worker->GetActorLocation().Z), State.F.Rotation(), false, true);
			// The moved case: the barricade lies 5 m aside on the near side, the mate will push it into the gap.
			State.Barricade = SpawnBarricade(World, State, At(State, 300.f, 500.f));
			State.Case = TEXT("MOVED barricade");
			Check(State, State.Barricade.IsValid() && State.Barricade->bVaultable, TEXT("a vaultable barricade 5 m aside"));
			Next(State, 1);
			return true;
		}
		case 1:
			if (State.StageTime < 1.5f)
			{
				return true; // the navmesh takes the walls in
			}
			Check(State, PathLength(World, State.Leader.Get(), At(State, WallAlong - 350.f, 0.f, 50.f), At(State, WallAlong + 350.f, 0.f, 50.f)) > 0.f,
				TEXT("the open gap is a navmesh path"));
			Relocation->ExecuteRelocate(State.Worker.Get(), State.Barricade.Get(), At(State, WallAlong, 0.f, 0.f) + FVector(0.f, 0.f, 0.f), GapYaw(State));
			Next(State, 2);
			return true;
		case 2:
		{
			ABarricadeActor* Barricade = State.Barricade.Get();
			if (Barricade && (Relocation->HasActiveTask(State.Worker.Get()) || Barricade->ActorHasTag(VaultNavigation::RelocatingTag)) && State.StageTime < 25.f)
			{
				return true;
			}
			Check(State, !Relocation->HasActiveTask(State.Worker.Get()), FString::Printf(TEXT("the mate pushed the barricade into the gap (%.1f s)"), State.StageTime));
			Next(State, 3);
			return true;
		}
		case 3:
			if (State.StageTime < 2.f)
			{
				return true; // the dynamic navmesh rebuilds round the set-down barricade
			}
			CheckGapNavigation(State, World);
			StartCrossing(State, true);
			Next(State, 4);
			return true;
		case 4:
			if (!WatchCrossing(State, true))
			{
				return true;
			}
			Next(State, 5);
			return true;
		case 5:
			if (State.StageTime < 0.5f)
			{
				return true; // the vault cooldown
			}
			StartCrossing(State, false);
			Next(State, 6);
			return true;
		case 6:
			if (!WatchCrossing(State, false))
			{
				return true;
			}
			// The placed case: a fresh barricade in the same gap.
			if (ABarricadeActor* Old = State.Barricade.Get())
			{
				Old->Destroy();
			}
			State.Barricade = SpawnBarricade(World, State, At(State, WallAlong, 0.f));
			State.Case = TEXT("PLACED barricade");
			Next(State, 7);
			return true;
		case 7:
			if (State.StageTime < 2.f)
			{
				return true;
			}
			CheckGapNavigation(State, World);
			StartCrossing(State, true);
			Next(State, 8);
			return true;
		case 8:
			if (!WatchCrossing(State, true))
			{
				return true;
			}
			Next(State, 9);
			return true;
		case 9:
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			StartCrossing(State, false);
			Next(State, 10);
			return true;
		case 10:
			if (!WatchCrossing(State, false))
			{
				return true;
			}
			return Finish(State, true);
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
		TEXT("CodexTactics.BarricadeVaultBackSmoke"),
		TEXT("Dev check (designer report 2026-10-22): a moved and a placed barricade in a wall gap are vaulted over and back; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
