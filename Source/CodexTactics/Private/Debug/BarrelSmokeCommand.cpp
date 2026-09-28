// Dev-only console command for a headless fuel barrel check on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.BarrelSmoke
// Spawns a barrel ahead of the leader and drives it through the player's path (click -> approach -> action menu):
// light it (one match spent, heat + light on), the leader warms up next to it, it burns out (charred, heat off),
// clicking the burnt barrel only posts the Godot line, and a leader without matches gets a greyed-out «Нет спичек».

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/BarrelActor.h"
#include "Interactables/HeatSourceComponent.h"
#include "Interactables/InteractionSubsystem.h"
#include "Survival/ColdSurvivalComponent.h"
#include "TimerManager.h"

namespace BarrelSmoke
{
	constexpr float NavWarmupSeconds = 3.f;
	constexpr float StepSeconds = 0.5f;
	constexpr float Timeout = 40.f;
	/** Short burn so the check sees the barrel go out. */
	constexpr float TestBurnDuration = 5.f;

	enum class EPhase : uint8 { ApproachFresh, WarmUp, BurnOut, ClickBurnt, NoMatches, Done };

	struct FState
	{
		EPhase Phase = EPhase::ApproachFresh;
		float Time = 0.f;
		float PhaseTime = 0.f;
		int32 MatchesBefore = 0;
		float ColdBefore = 0.f;
		TWeakObjectPtr<ABarrelActor> Barrel;
		TWeakObjectPtr<ABarrelActor> SecondBarrel;
		bool bIgniteOk = false;
		bool bWarmOk = false;
		bool bBurnOutOk = false;
		bool bBurntClickOk = false;
		bool bNoMatchesOk = false;
	};

	void Finish(FState& State)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke ignite=%d warm=%d burnout=%d burntClick=%d noMatches=%d"), State.bIgniteOk ? 1 : 0,
			State.bWarmOk ? 1 : 0, State.bBurnOutOk ? 1 : 0, State.bBurntClickOk ? 1 : 0, State.bNoMatchesOk ? 1 : 0);
		const bool bPass = State.bIgniteOk && State.bWarmOk && State.bBurnOutOk && State.bBurntClickOk && State.bNoMatchesOk;
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), bPass ? TEXT("PASS") : TEXT("FAIL"));
		State.Phase = EPhase::Done;
		FPlatformMisc::RequestExit(false, TEXT("BarrelSmoke"));
	}

	void Enter(FState& State, EPhase Phase)
	{
		State.Phase = Phase;
		State.PhaseTime = 0.f;
	}

	ABarrelActor* SpawnBarrel(UWorld* World, const AOperativeCharacter* Leader, float Side)
	{
		const FVector Location = Leader->GetActorLocation() + Leader->GetActorForwardVector() * 450.f
			+ Leader->GetActorRightVector() * Side + FVector(0.f, 0.f, -20.f);
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		ABarrelActor* Barrel = World->SpawnActor<ABarrelActor>(Location, FRotator::ZeroRotator, Params);
		if (Barrel)
		{
			Barrel->BurnDuration = TestBurnDuration;
		}
		return Barrel;
	}

	/** Returns false when finished. */
	bool Step(UWorld* World, FState& State)
	{
		State.Time += StepSeconds;
		State.PhaseTime += StepSeconds;
		UInteractionSubsystem* Interactions = World->GetSubsystem<UInteractionSubsystem>();
		AOperativeCharacter* Leader = World->GetSubsystem<USquadSubsystem>()->GetLeader();
		ABarrelActor* Barrel = State.Barrel.Get();
		if (!Leader || !Barrel || State.Time > Timeout)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke stopped in phase %d (leader=%d barrel=%d time=%.1f)"),
				static_cast<int32>(State.Phase), Leader ? 1 : 0, Barrel ? 1 : 0, State.Time);
			Finish(State);
			return false;
		}

		switch (State.Phase)
		{
		case EPhase::ApproachFresh:
			if (Interactions->IsActionMenuOpen())
			{
				const FActionMenuSpec& Menu = Interactions->GetActionMenu();
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke menu \"%s\" / \"%s\" disabled=%d"), *Menu.Title.ToString(),
					*Menu.ConfirmText.ToString(), Menu.bConfirmDisabled ? 1 : 0);
				State.MatchesBefore = Leader->MatchesCount;
				Interactions->ConfirmActionMenu();
				State.bIgniteOk = !Menu.bConfirmDisabled && Barrel->IsBurning() && Barrel->HeatSource->IsHeatActive()
					&& Leader->MatchesCount == State.MatchesBefore - 1;
				Leader->ColdLevel = 50.f;
				State.ColdBefore = Leader->ColdLevel;
				Enter(State, EPhase::WarmUp);
			}
			return true;

		case EPhase::WarmUp:
			if (State.PhaseTime >= 2.f)
			{
				const bool bNearHeat = Leader->ColdSurvival && Leader->ColdSurvival->IsNearHeatSource();
				State.bWarmOk = bNearHeat && Leader->ColdLevel < State.ColdBefore - 5.f;
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke warm: nearHeat=%d cold %.1f -> %.1f, burn left %.1fs"), bNearHeat ? 1 : 0,
					State.ColdBefore, Leader->ColdLevel, Barrel->GetBurnTimeLeft());
				Enter(State, EPhase::BurnOut);
			}
			return true;

		case EPhase::BurnOut:
			if (!Barrel->IsBurning())
			{
				State.bBurnOutOk = Barrel->IsBurnt() && !Barrel->HeatSource->IsHeatActive();
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke burnt out at %.1fs, heat=%d"), State.Time, Barrel->HeatSource->IsHeatActive() ? 1 : 0);
				Interactions->RequestInteraction(Barrel);
				Enter(State, EPhase::ClickBurnt);
			}
			return true;

		case EPhase::ClickBurnt:
			// Not relocatable yet -> Godot posts «Горючее в этой бочке уже полностью выгорело.» instead of a menu.
			State.bBurntClickOk = !Interactions->IsActionMenuOpen() && !Interactions->GetPendingInteraction();
			Leader->MatchesCount = 0;
			State.SecondBarrel = SpawnBarrel(World, Leader, 300.f);
			if (State.SecondBarrel.IsValid())
			{
				Interactions->RequestInteraction(State.SecondBarrel.Get());
			}
			Enter(State, EPhase::NoMatches);
			return true;

		case EPhase::NoMatches:
			if (Interactions->IsActionMenuOpen())
			{
				const FActionMenuSpec& Menu = Interactions->GetActionMenu();
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke no-match menu \"%s\" disabled=%d"), *Menu.ConfirmText.ToString(),
					Menu.bConfirmDisabled ? 1 : 0);
				Interactions->ConfirmActionMenu(); // must do nothing
				State.bNoMatchesOk = Menu.bConfirmDisabled && State.SecondBarrel.IsValid() && !State.SecondBarrel->IsBurning();
				Finish(State);
				return false;
			}
			return true;

		default:
			return false;
		}
	}

	void Start(UWorld* World)
	{
		AOperativeCharacter* Leader = World->GetSubsystem<USquadSubsystem>()->GetLeader();
		UInteractionSubsystem* Interactions = World->GetSubsystem<UInteractionSubsystem>();
		TSharedRef<FState> State = MakeShared<FState>();
		if (Leader)
		{
			State->Barrel = SpawnBarrel(World, Leader, 0.f);
		}
		if (!State->Barrel.IsValid())
		{
			Finish(*State);
			return;
		}
		Interactions->RequestInteraction(State->Barrel.Get());

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
		TEXT("CodexTactics.BarrelSmoke"),
		TEXT("Dev check: fuel barrel via the action menu (light, warmth, burn-out, burnt click, no matches); logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
