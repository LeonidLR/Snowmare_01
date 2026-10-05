// Dev-only headless check of the preparation set-up (user decision 2026-10-05) on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.PrepDeploySmoke -Log Smoke-PrepDeploy.log
// Preparation phase; only the commander carries a barricade. The player (commander selected) marks a spot 4 m past the
// operative farthest from him: that operative — the closest to the spot — gets the shared barricade, runs (sprint) there
// and sets it up. PASS when the right operative sprinted and the barricade stands on the spot.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/BarricadeActor.h"
#include "Interactables/RelocationSubsystem.h"

namespace PrepDeploySmoke
{
	struct FState
	{
		float Time = 0.f;
		int32 Stage = 0;
		TWeakObjectPtr<AOperativeCharacter> Commander;
		TWeakObjectPtr<AOperativeCharacter> Expected;
		FVector Spot = FVector::ZeroVector;
		bool bSprinted = false;
	};

	bool Finish(bool bPass)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), bPass ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("PrepDeploySmoke"));
		return false;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += 0.1f; // fixed step like the other smokes
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		URelocationSubsystem* Relocation = World->GetSubsystem<URelocationSubsystem>();
		if (State.Stage == 0)
		{
			if (State.Time < 3.f)
			{
				return true;
			}
			Flow->TriggerCombatZone();
			Flow->FinishCutscene();
			const TArray<AOperativeCharacter*> Members = Squad->GetMembers();
			AOperativeCharacter* Commander = Squad->GetLeader();
			AOperativeCharacter* Far = nullptr;
			for (AOperativeCharacter* Member : Members)
			{
				Member->BarricadesCount = 0;
				Member->ColdLevel = 0.f;
				if (Member != Commander && (!Far || FVector::Dist2D(Member->GetActorLocation(), Commander->GetActorLocation())
					> FVector::Dist2D(Far->GetActorLocation(), Commander->GetActorLocation())))
				{
					Far = Member;
				}
			}
			if (Flow->GetPhase() != ECodexGamePhase::Preparation || !Commander || !Far)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke FAIL: preparation phase %d, commander / far operative"), static_cast<int32>(Flow->GetPhase()));
				return Finish(false);
			}
			Commander->BarricadesCount = 1;
			const FVector Away = (Far->GetActorLocation() - Commander->GetActorLocation()).GetSafeNormal2D();
			const FVector Desired = Far->GetActorLocation() + Away * 400.f;
			State.Spot = SmokeUtils::ClearPoint(World, Far->GetActorLocation(), Desired);
			State.Spot.Z = Far->GetActorLocation().Z - Far->GetSimpleCollisionHalfHeight();
			State.Commander = Commander;
			State.Expected = Far;
			Relocation->StartDeployPlacement(EDeployableType::Barricade, Commander);
			Relocation->UpdatePreview(State.Spot);
			Relocation->ConfirmPlacement(State.Spot);
			Relocation->ConfirmPlacement(State.Spot);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: commander marks a barricade %.1f m from him, %.1f m from %s; tasks %d"),
				FVector::Dist2D(State.Spot, Commander->GetActorLocation()) / 100.f, FVector::Dist2D(State.Spot, Far->GetActorLocation()) / 100.f,
				*Far->DisplayName.ToString(), Relocation->GetActiveDeployCount());
			State.Stage = 1;
			State.Time = 0.f;
			return true;
		}
		AOperativeCharacter* Expected = State.Expected.Get();
		State.bSprinted |= Expected && Expected->IsSprinting();
		if (Relocation->GetActiveDeployCount() > 0 && State.Time < 20.f)
		{
			return true;
		}
		const ABarricadeActor* Placed = nullptr;
		for (TActorIterator<ABarricadeActor> It(World); It; ++It)
		{
			if (FVector::Dist2D(It->GetActorLocation(), State.Spot) < 60.f)
			{
				Placed = *It;
			}
		}
		const bool bCommanderStayed = State.Commander.IsValid() && State.Commander->BarricadesCount == 0;
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: barricade on the spot %d, set up by the closest (%s) running %d, commander handed it over %d (%.1f s)"),
			Placed && State.bSprinted && bCommanderStayed ? TEXT("ok  ") : TEXT("FAIL"), Placed ? 1 : 0,
			Expected ? *Expected->DisplayName.ToString() : TEXT("-"), State.bSprinted ? 1 : 0, bCommanderStayed ? 1 : 0, State.Time);
		return Finish(Placed && State.bSprinted && bCommanderStayed);
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), 0.1f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.PrepDeploySmoke"),
		TEXT("Dev check: in the preparation the closest operative runs to set up a marked barricade; PASS / FAIL."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
