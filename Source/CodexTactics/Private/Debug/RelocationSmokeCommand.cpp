// Dev-only console command for a headless object relocation check on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.RelocationSmoke
// Spawns a barrel ahead of the leader, opens its action menu, presses «Вытолкать», places it 6 m to the side:
// the leader walks up, pushes it (carry speed) and sets it down there with collision restored. Then a frozen leader
// (85 % cold) is refused, and placement can be cancelled. Last, a hit while pushing drops the barrel (Godot
// take_damage -> _cancel_or_finalize_active_relocates_for_combat). Sprint 06-E/F: while pushing the barrel never comes
// inside the push distance (no clipping), and RMB (CancelActiveTask) sets it down mid-push.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/BarrelActor.h"
#include "Interactables/InteractionSubsystem.h"
#include "Interactables/RelocationSubsystem.h"
#include "TimerManager.h"

namespace RelocationSmoke
{
	constexpr float NavWarmupSeconds = 3.f;
	constexpr float StepSeconds = 0.25f;
	constexpr float Timeout = 45.f;
	constexpr float PlaceTolerance = 30.f;

	enum class EPhase : uint8 { OpenMenu, Moving, HitDrop, CancelPush, Done };

	struct FState
	{
		EPhase Phase = EPhase::OpenMenu;
		float Time = 0.f;
		FVector Target = FVector::ZeroVector;
		TWeakObjectPtr<ABarrelActor> Barrel;
		bool bMenuOk = false;
		bool bCarrySeen = false;
		bool bFirstPassOk = false;
		bool bHitOk = false;
		float MinGap = TNumericLimits<float>::Max();
		int32 PushSteps = 0;
	};

	void Finish(bool bPass, const TCHAR* Reason)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s"), Reason);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), bPass ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("RelocationSmoke"));
	}

	/** Returns false when finished. */
	bool Step(UWorld* World, FState& State)
	{
		State.Time += StepSeconds;
		UInteractionSubsystem* Interactions = World->GetSubsystem<UInteractionSubsystem>();
		URelocationSubsystem* Relocation = World->GetSubsystem<URelocationSubsystem>();
		AOperativeCharacter* Leader = World->GetSubsystem<USquadSubsystem>()->GetLeader();
		ABarrelActor* Barrel = State.Barrel.Get();
		if (!Leader || !Barrel || State.Time > Timeout)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke timeout: phase=%d tasks=%d leader=(%.0f, %.0f) barrel=(%.0f, %.0f)"),
				static_cast<int32>(State.Phase), Relocation->GetActiveTaskCount(), Leader ? Leader->GetActorLocation().X : 0.f,
				Leader ? Leader->GetActorLocation().Y : 0.f, Barrel ? Barrel->GetActorLocation().X : 0.f, Barrel ? Barrel->GetActorLocation().Y : 0.f);
			Finish(false, TEXT("stopped"));
			return false;
		}

		if (State.Phase == EPhase::OpenMenu)
		{
			if (!Interactions->IsActionMenuOpen())
			{
				return true;
			}
			const FActionMenuSpec& Menu = Interactions->GetActionMenu();
			State.bMenuOk = Menu.bAllowRelocate;
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke menu \"%s\" relocate=\"%s\" (%d)"), *Menu.Title.ToString(),
				*Menu.RelocateText.ToString(), Menu.bAllowRelocate ? 1 : 0);
			Interactions->RelocateActionMenu();
			if (!Relocation->IsPlacing())
			{
				Finish(false, TEXT("placement mode did not start"));
				return false;
			}
			State.Target = Barrel->GetActorLocation() + Leader->GetActorRightVector() * 600.f;
			Relocation->UpdatePreview(State.Target);
			Relocation->RotatePreview(+2);
			Relocation->ConfirmPlacement(State.Target);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke placement confirmed at (%.0f, %.0f), tasks=%d"), State.Target.X, State.Target.Y,
				Relocation->GetActiveTaskCount());
			State.Phase = EPhase::Moving;
			return true;
		}

		if (State.Phase == EPhase::Moving)
		{
			State.bCarrySeen |= Leader->bCarrying;
			if (Relocation->GetActiveTaskCount() > 0)
			{
				return true;
			}
			const float Error = FVector::Dist2D(Barrel->GetActorLocation(), State.Target);
			const bool bPlaced = Error <= PlaceTolerance && Barrel->Box->IsCollisionEnabled() && !Leader->bCarrying;
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke placed at %.1fs: error=%.1f yaw=%.0f collision=%d carried=%d carrying=%d"),
				State.Time, Error, Barrel->GetActorRotation().Yaw, Barrel->Box->IsCollisionEnabled() ? 1 : 0, State.bCarrySeen ? 1 : 0,
				Leader->bCarrying ? 1 : 0);

			// A frozen operative cannot lift; placement can be cancelled.
			Leader->ColdLevel = 85.f;
			const bool bColdRefused = !Relocation->StartRelocate(Barrel, Leader) && !Relocation->IsPlacing();
			Leader->ColdLevel = 0.f;
			const bool bStarted = Relocation->StartRelocate(Barrel, Leader);
			Relocation->CancelPlacement();
			const bool bCancelled = bStarted && !Relocation->IsPlacing();
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke menu=%d placed=%d pushed=%d coldRefused=%d cancel=%d"), State.bMenuOk ? 1 : 0,
				bPlaced ? 1 : 0, State.bCarrySeen ? 1 : 0, bColdRefused ? 1 : 0, bCancelled ? 1 : 0);
			State.bFirstPassOk = State.bMenuOk && bPlaced && State.bCarrySeen && bColdRefused && bCancelled;
			// Push it back to the start; the hit comes while the leader carries it.
			Relocation->StartRelocate(Barrel, Leader);
			State.Target = Barrel->GetActorLocation() - Leader->GetActorRightVector() * 600.f;
			Relocation->UpdatePreview(State.Target);
			Relocation->ConfirmPlacement(State.Target);
			State.Phase = EPhase::HitDrop;
			return true;
		}

		if (State.Phase == EPhase::HitDrop)
		{
			if (!Leader->bCarrying)
			{
				return true;
			}
			Leader->ForcedDodgeRollForTesting = 0.f; // no dodge
			Leader->TakeHit(5.f, TEXT("Smoke"), false);
			const bool bDropped = !Leader->bCarrying && Relocation->GetActiveTaskCount() == 0 && Barrel->Box->IsCollisionEnabled();
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke hit while pushing: dropped=%d"), bDropped ? 1 : 0);
			State.bHitOk = bDropped;
			// Push it again; RMB cancels after a second of pushing.
			Relocation->StartRelocate(Barrel, Leader);
			State.Target = Barrel->GetActorLocation() + Leader->GetActorRightVector() * 900.f;
			Relocation->UpdatePreview(State.Target);
			Relocation->ConfirmPlacement(State.Target);
			State.Phase = EPhase::CancelPush;
			return true;
		}

		if (State.Phase == EPhase::CancelPush)
		{
			if (!Leader->bCarrying)
			{
				return true;
			}
			const FVector Forward = Leader->GetActorForwardVector().GetSafeNormal2D();
			const float Along = FVector::DotProduct(Barrel->GetActorLocation() - Leader->GetActorLocation(), Forward);
			State.MinGap = FMath::Min(State.MinGap, Along - URelocationSubsystem::GetPushOffset(*Leader, *Barrel, Forward));
			if (++State.PushSteps < 4)
			{
				return true;
			}
			const bool bCancelled = Relocation->CancelActiveTask(Leader);
			const bool bCancelOk = bCancelled && !Leader->bCarrying && Relocation->GetActiveTaskCount() == 0
				&& Barrel->Box->IsCollisionEnabled() && FVector::Dist2D(Barrel->GetActorLocation(), State.Target) > 100.f;
			// One frame of the worker's walk may come after the object's tick.
			const bool bGapOk = State.MinGap >= -20.f;
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: RMB cancel mid-push (cancelled=%d carrying=%d tasks=%d collision=%d)"),
				bCancelOk ? TEXT("ok  ") : TEXT("FAIL"), bCancelled ? 1 : 0, Leader->bCarrying ? 1 : 0, Relocation->GetActiveTaskCount(),
				Barrel->Box->IsCollisionEnabled() ? 1 : 0);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: push gap min %.1f cm past the push distance"), bGapOk ? TEXT("ok  ") : TEXT("FAIL"),
				State.MinGap);
			Finish(State.bFirstPassOk && State.bHitOk && bCancelOk && bGapOk, TEXT("relocation finished"));
			State.Phase = EPhase::Done;
			return false;
		}
		return false;
	}

	void Start(UWorld* World)
	{
		AOperativeCharacter* Leader = World->GetSubsystem<USquadSubsystem>()->GetLeader();
		if (!Leader)
		{
			Finish(false, TEXT("no leader"));
			return;
		}
		TSharedRef<FState> State = MakeShared<FState>();
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		const FVector Spot = Leader->GetActorLocation() + Leader->GetActorForwardVector() * 450.f + FVector(0.f, 0.f, -20.f);
		State->Barrel = World->SpawnActor<ABarrelActor>(Spot, FRotator::ZeroRotator, Params);
		if (!State->Barrel.IsValid())
		{
			Finish(false, TEXT("barrel spawn failed"));
			return;
		}
		World->GetSubsystem<UInteractionSubsystem>()->RequestInteraction(State->Barrel.Get());

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
		}), NavWarmupSeconds, false);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.RelocationSmoke"),
		TEXT("Dev check: push a barrel to a new spot via «Вытолкать», cold refusal, placement cancel; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
