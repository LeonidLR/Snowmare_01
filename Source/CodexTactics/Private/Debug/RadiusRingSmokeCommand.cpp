// Dev-only console command for a headless radius ring check on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.RadiusRingSmoke
// Outside the pause the ring is hidden; the tactical pause shows the 12 m green order ring around the leader; a
// barricade set-up in the pause shows the worker's radius (green inside, red outside); a barrel relocation shows it
// cyan; releasing the pause hides the ring (Godot main.gd radius_ring, _update_relocate_radius_ring,
// _set_ghost_material_valid).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/BarrelActor.h"
#include "Interactables/RadiusRingSubsystem.h"
#include "Interactables/RelocationSubsystem.h"
#include "TimerManager.h"

namespace RadiusRingSmoke
{
	constexpr float StepSeconds = 0.25f;

	struct FState
	{
		float Time = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		TWeakObjectPtr<ABarrelActor> Barrel;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("RadiusRingSmoke"));
		return false;
	}

	bool RingShows(const URadiusRingSubsystem* Rings, ERadiusRingMode Mode, float Radius, const FLinearColor& Color)
	{
		const ARadiusRingActor* Ring = Rings->GetRing();
		return Rings->GetMode() == Mode && Ring && !Ring->IsHidden() && FMath::IsNearlyEqual(Ring->GetRadius(), Radius, 1.f)
			&& Ring->GetColor().Equals(Color);
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time < 3.f)
		{
			return World != nullptr;
		}
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		URelocationSubsystem* Relocation = World->GetSubsystem<URelocationSubsystem>();
		URadiusRingSubsystem* Rings = World->GetSubsystem<URadiusRingSubsystem>();
		AOperativeCharacter* Leader = World->GetSubsystem<USquadSubsystem>()->GetLeader();
		if (!Flow || !Relocation || !Rings || !Leader)
		{
			Check(State, false, TEXT("flow, relocation, rings, leader"));
			return Finish(State, false);
		}
		const float PauseRadius = Flow->GetConfig().PauseOrderRadius;
		switch (State.Stage++)
		{
		case 0:
			Check(State, Rings->GetMode() == ERadiusRingMode::Hidden, TEXT("no ring outside the pause"));
			Flow->TriggerCombatZone();
			Flow->FinishCutscene();
			Flow->FinishPreparation();
			Flow->ToggleTacticalPause();
			return true;
		case 1:
		{
			Check(State, Flow->GetCombatMode() == ECodexCombatMode::TacticalPause, TEXT("tactical pause"));
			const ARadiusRingActor* Ring = Rings->GetRing();
			const bool bOk = RingShows(Rings, ERadiusRingMode::PauseOrders, PauseRadius, URadiusRingSubsystem::Green);
			Check(State, bOk, FString::Printf(TEXT("pause: green order ring r=%.0f (expected %.0f)"), Ring ? Ring->GetRadius() : -1.f, PauseRadius));
			Leader->BarricadesCount = FMath::Max(Leader->BarricadesCount, 1);
			Check(State, Relocation->StartDeployPlacement(EDeployableType::Barricade, Leader), TEXT("barricade placement started"));
			Relocation->UpdatePreview(Leader->GetActorLocation() + Leader->GetActorForwardVector() * 300.f);
			return true;
		}
		case 2:
		{
			const float Radius = Relocation->GetRadius(*Leader);
			Check(State, RingShows(Rings, ERadiusRingMode::Deploy, Radius, URadiusRingSubsystem::Green),
				FString::Printf(TEXT("deploy inside: green worker ring r=%.0f"), Radius));
			Relocation->UpdatePreview(Relocation->GetOrigin(*Leader) + Leader->GetActorForwardVector() * (Radius + 400.f));
			return true;
		}
		case 3:
		{
			Check(State, RingShows(Rings, ERadiusRingMode::Deploy, Relocation->GetRadius(*Leader), URadiusRingSubsystem::Red),
				TEXT("deploy outside the radius: red ring"));
			Relocation->CancelPlacement();
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
			State.Barrel = World->SpawnActor<ABarrelActor>(Leader->GetActorLocation() + Leader->GetActorRightVector() * 250.f + FVector(0.f, 0.f, -20.f),
				FRotator::ZeroRotator, Params);
			Check(State, State.Barrel.IsValid() && Relocation->StartRelocate(State.Barrel.Get(), Leader), TEXT("barrel relocation started"));
			Relocation->UpdatePreview(Leader->GetActorLocation() - Leader->GetActorRightVector() * 300.f);
			return true;
		}
		case 4:
			Check(State, RingShows(Rings, ERadiusRingMode::Relocation, Relocation->GetRadius(*Leader), URadiusRingSubsystem::Cyan),
				TEXT("relocation inside: cyan ring"));
			Relocation->CancelPlacement();
			Flow->ToggleTacticalPause();
			return true;
		default:
			Check(State, Flow->GetCombatMode() != ECodexCombatMode::TacticalPause && Rings->GetMode() == ERadiusRingMode::Hidden
				&& Rings->GetRing() && Rings->GetRing()->IsHidden(), TEXT("pause released: ring hidden"));
			return Finish(State, true);
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
		TEXT("CodexTactics.RadiusRingSmoke"),
		TEXT("Dev check: pause order ring, deploy / relocation radius ring colours, hidden after the pause; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
