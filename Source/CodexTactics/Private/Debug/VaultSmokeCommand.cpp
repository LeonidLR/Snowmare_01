// Dev-only console command for a headless check on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.VaultSmoke
// Godot try_vault_obstacle: a barricade straight across the leader's way — the path goes over it (vault nav area),
// the leader vaults (arc, collision off) and walks on to the destination behind it. Then (user report 2026-10-04: stuck on
// top of a barricade after a vault, a cutter's pounce too): a vault along a barricade's 3 m length never lands on / in
// it, an operative put on a barricade top jumps down by himself, and so does a cutter.

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
#include "Interactables/VaultNavigation.h"
#include "Characters/EnemyCharacter.h"
#include "Combat/WaveSubsystem.h"
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
		TWeakObjectPtr<ABarricadeActor> Lengthwise;
		TWeakObjectPtr<AEnemyCharacter> Cutter;
		float GroundZ = 0.f;
	};

	/** Off every obstacle top and back at ground height. */
	bool IsDown(const ACharacter* Character, float GroundZ)
	{
		return Character && !VaultNavigation::IsStandingOnObstacle(*Character)
			&& FMath::Abs(Character->GetActorLocation().Z - Character->GetSimpleCollisionHalfHeight() - GroundZ) < 40.f;
	}

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
			// A barricade lengthwise ahead: its 3 m run straight away from him, the near end 1 m off.
			const FVector Forward = Leader->GetActorForwardVector().GetSafeNormal2D();
			State.GroundZ = Leader->GetActorLocation().Z - Leader->GetSimpleCollisionHalfHeight();
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			State.Lengthwise = World->SpawnActor<ABarricadeActor>(FVector(Leader->GetActorLocation().X, Leader->GetActorLocation().Y, State.GroundZ + 50.f)
				+ Forward * 250.f, FRotator(0.f, Forward.Rotation().Yaw, 0.f), Params);
			State.Stage = 3;
			State.StageTime = 0.f;
			return true;
		}
		case 3:
		{
			AOperativeCharacter* Leader = State.Leader.Get();
			if (State.StageTime < 1.f)
			{
				return true;
			}
			if (State.StageTime < 1.05f)
			{
				const bool bVaulted = Leader->TryVault(Leader->GetActorForwardVector(), true);
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke lengthwise vault: %s"), bVaulted ? TEXT("vaults, landing past the far end") : TEXT("refused"));
				return true;
			}
			if (Leader->IsVaulting() || State.StageTime < 3.f)
			{
				return true;
			}
			Check(State, IsDown(Leader, State.GroundZ), FString::Printf(TEXT("a vault along the barricade's length does not end on it (z %.0f above the ground)"),
				Leader->GetActorLocation().Z - Leader->GetSimpleCollisionHalfHeight() - State.GroundZ));
			// Put him on top of it.
			ABarricadeActor* Barricade = State.Lengthwise.Get();
			Leader->TeleportTo(Barricade->GetActorLocation() + FVector(0.f, 0.f, 50.f + Leader->GetSimpleCollisionHalfHeight() + 5.f), Leader->GetActorRotation());
			State.Stage = 4;
			State.StageTime = 0.f;
			return true;
		}
		case 4:
		{
			AOperativeCharacter* Leader = State.Leader.Get();
			State.bSawVault |= VaultNavigation::IsStandingOnObstacle(*Leader);
			if (!IsDown(Leader, State.GroundZ) && State.StageTime < 3.f)
			{
				return true;
			}
			Check(State, IsDown(Leader, State.GroundZ), FString::Printf(TEXT("put on the barricade top he jumps down by himself (%.1f s)"), State.StageTime));
			ABarricadeActor* Barricade = State.Lengthwise.Get();
			State.Cutter = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::Cutter,
				Barricade->GetActorLocation() + FVector(0.f, 0.f, 50.f + 100.f + 5.f));
			State.Stage = 5;
			State.StageTime = 0.f;
			return true;
		}
		case 5:
		{
			AEnemyCharacter* Cutter = State.Cutter.Get();
			if (Cutter && !IsDown(Cutter, State.GroundZ) && State.StageTime < 3.f)
			{
				return true;
			}
			Check(State, IsDown(Cutter, State.GroundZ), FString::Printf(TEXT("a cutter on the barricade top jumps down (%.1f s)"), State.StageTime));
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
