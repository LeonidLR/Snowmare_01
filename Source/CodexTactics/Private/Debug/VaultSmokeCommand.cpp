// Dev-only console command for a headless check on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.VaultSmoke
// Godot try_vault_obstacle: a barricade straight across the leader's way — the path goes over it (vault nav area),
// the leader vaults (arc, collision off) and walks on to the destination behind it.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/BarricadeActor.h"
#include "TimerManager.h"

namespace VaultSmoke
{
	constexpr float StepSeconds = 0.05f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		bool bSawVault = false;
		float MaxLift = 0.f;
		float StartZ = 0.f;
		FVector Destination = FVector::ZeroVector;
		TWeakObjectPtr<AOperativeCharacter> Leader;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("VaultSmoke"));
		return false;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 40.f)
		{
			return World ? Finish(State, false) : false;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		switch (State.Stage)
		{
		case 0:
		{
			if (State.StageTime < 3.f || !Squad->GetLeader())
			{
				return true;
			}
			AOperativeCharacter* Leader = Squad->GetLeader();
			Squad->SetFollowersHolding(true);
			const FVector Forward = Leader->GetActorForwardVector();
			const FVector Feet = Leader->GetActorLocation() - FVector(0.f, 0.f, Leader->GetSimpleCollisionHalfHeight());
			// A 3 m barricade across the way, 2.5 m ahead; the destination 3 m behind it.
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ABarricadeActor* Barricade = World->SpawnActor<ABarricadeActor>(Feet + Forward * 250.f + FVector(0.f, 0.f, 50.f),
				FRotator(0.f, Forward.Rotation().Yaw + 90.f, 0.f), Params);
			Check(State, Barricade && Barricade->bVaultable, TEXT("vaultable barricade across the way"));
			State.Leader = Leader;
			State.StartZ = Leader->GetActorLocation().Z;
			State.Destination = Leader->GetActorLocation() + Forward * 550.f;
			State.Stage = 1;
			State.StageTime = 0.f;
			return true;
		}
		case 1:
			// Let the dynamic navmesh take the barricade's vault area in.
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			Check(State, State.Leader->OrderMoveTo(State.Destination, false) == EOperativeOrderResult::Accepted, TEXT("order across the barricade accepted"));
			State.Stage = 2;
			State.StageTime = 0.f;
			return true;
		case 2:
		{
			AOperativeCharacter* Leader = State.Leader.Get();
			State.bSawVault |= Leader->IsVaulting();
			State.MaxLift = FMath::Max(State.MaxLift, Leader->GetActorLocation().Z - State.StartZ);
			const float Remaining = FVector::Dist2D(Leader->GetActorLocation(), State.Destination);
			if ((Remaining > 120.f || Leader->IsVaulting()) && State.StageTime < 8.f)
			{
				return true;
			}
			Check(State, State.bSawVault, TEXT("the leader vaulted"));
			Check(State, State.MaxLift > 60.f, FString::Printf(TEXT("up over the barricade (%.0f cm lift)"), State.MaxLift));
			Check(State, Remaining <= 120.f, FString::Printf(TEXT("walked on to the destination (%.0f cm off)"), Remaining));
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
		TEXT("CodexTactics.VaultSmoke"),
		TEXT("Dev check: the leader vaults a barricade across his way and walks on; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
