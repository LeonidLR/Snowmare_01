// Dev-only console command for a headless check of the melee pack tactics (UEnemyTacticsSubsystem) on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.EnemyTacticsSmoke
// Six hounds come at a three-man line (unkillable, cease-fire): they spread over at least two operatives (cap per
// target), part of them is sent round the flanks and gets to the squad's side / rear; a badly hurt hound breaks
// (morale) and runs back to the pack.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/EnemyTacticsSubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/InteractableActor.h"
#include "TimerManager.h"

namespace EnemyTacticsSmoke
{
	constexpr float StepSeconds = 0.25f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		TArray<TWeakObjectPtr<AEnemyCharacter>> Hounds;
		FVector Centre = FVector::ZeroVector;
		FVector Forward = FVector::ForwardVector;
		float SideReached = 0.f;
		TWeakObjectPtr<AEnemyCharacter> Broken;
		float BrokenStartDistance = 0.f;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("EnemyTacticsSmoke"));
		return false;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 60.f)
		{
			return World ? Finish(State, false) : false;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UEnemyTacticsSubsystem* Tactics = World->GetSubsystem<UEnemyTacticsSubsystem>();
		switch (State.Stage)
		{
		case 0:
		{
			if (State.StageTime < 3.f)
			{
				return true;
			}
			const TArray<AOperativeCharacter*> Members = Squad->GetMembers();
			if (Members.Num() < 3 || !Tactics)
			{
				return Finish(State, false);
			}
			Squad->SetFollowersHolding(true);
			for (TActorIterator<AInteractableActor> It(World); It; ++It)
			{
				if (It->ObjectType == EInteractableType::Generator)
				{
					It->bGeneratorBroken = true; // small enemies would rightly go for it first
				}
			}
			const FVector Origin = Members[0]->GetActorLocation();
			State.Forward = Members[0]->GetActorForwardVector().GetSafeNormal2D();
			const FVector Right = Members[0]->GetActorRightVector().GetSafeNormal2D();
			Members[1]->TeleportTo(Origin + Right * 300.f, Members[0]->GetActorRotation(), false, true);
			Members[2]->TeleportTo(Origin - Right * 300.f, Members[0]->GetActorRotation(), false, true);
			for (AOperativeCharacter* Member : Members)
			{
				Member->HealthComponent->SetMaxHealth(100000.f);
				Member->bTacticalCeaseFire = true;
			}
			State.Centre = Origin;
			for (int32 Index = 0; Index < 6; ++Index)
			{
				const FVector Spot = Origin + State.Forward * 2000.f + Right * (-250.f + Index * 100.f);
				State.Hounds.Add(World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::FrostHound, Spot));
			}
			State.Stage = 1;
			State.StageTime = 0.f;
			return true;
		}
		case 1:
		{
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			Tactics->Refresh();
			TMap<AOperativeCharacter*, int32> PerTarget;
			int32 Flankers = 0;
			int32 Ordered = 0;
			for (const TWeakObjectPtr<AEnemyCharacter>& Hound : State.Hounds)
			{
				FEnemyTacticOrder Order;
				if (Hound.IsValid() && Tactics->GetOrder(Hound.Get(), Order) && Order.Target.IsValid())
				{
					++Ordered;
					PerTarget.FindOrAdd(Order.Target.Get())++;
					Flankers += Order.Role == EEnemyTacticRole::Flank ? 1 : 0;
				}
			}
			Check(State, Ordered == 6, FString::Printf(TEXT("all six hounds under the pack coordinator (%d)"), Ordered));
			Check(State, PerTarget.Num() >= 2, FString::Printf(TEXT("spread over %d operatives (cap 3 per target)"), PerTarget.Num()));
			Check(State, Flankers >= 2, FString::Printf(TEXT("%d sent round the flanks"), Flankers));
			State.Stage = 2;
			State.StageTime = 0.f;
			return true;
		}
		case 2:
		{
			// Someone gets to the squad's side or rear (outside its front 90 degrees) within 6 m.
			for (const TWeakObjectPtr<AEnemyCharacter>& Hound : State.Hounds)
			{
				if (Hound.IsValid())
				{
					const FVector To = Hound->GetActorLocation() - State.Centre;
					if (To.Size2D() < 600.f)
					{
						State.SideReached = FMath::Max(State.SideReached, 1.f - static_cast<float>(FVector::DotProduct(To.GetSafeNormal2D(), State.Forward)));
					}
				}
			}
			if (State.StageTime < 10.f && State.SideReached < 1.f)
			{
				return true;
			}
			Check(State, State.SideReached >= 1.f, FString::Printf(TEXT("a hound reached the squad's side / rear (%.2f, flank orders %d, backstabs %d)"),
				State.SideReached, Tactics->GetFlankOrders(), Tactics->GetBackstabs()));
			// Morale: one hound down to 15 % breaks and runs.
			for (const TWeakObjectPtr<AEnemyCharacter>& Hound : State.Hounds)
			{
				if (Hound.IsValid() && !Hound->IsDying())
				{
					State.Broken = Hound;
					break;
				}
			}
			AEnemyCharacter* Broken = State.Broken.Get();
			if (!Broken)
			{
				return Finish(State, false);
			}
			UHealthComponent* Health = Broken->GetHealthComponent();
			FDamageSpec Spec;
			Spec.Amount = Health->GetCurrentHealth() - Health->GetMaxHealth() * 0.15f;
			Spec.AttackerSource = TEXT("smoke");
			Health->ApplyDamage(Spec);
			Tactics->Refresh();
			FEnemyTacticOrder Order;
			Check(State, Tactics->GetOrder(Broken, Order) && Order.Role == EEnemyTacticRole::FallBack,
				TEXT("a hound at 15 % health breaks and falls back"));
			State.BrokenStartDistance = FVector::Dist2D(Broken->GetActorLocation(), State.Centre);
			State.Stage = 3;
			State.StageTime = 0.f;
			return true;
		}
		case 3:
		{
			if (State.StageTime < 2.5f)
			{
				return true;
			}
			AEnemyCharacter* Broken = State.Broken.Get();
			const float Distance = Broken ? FVector::Dist2D(Broken->GetActorLocation(), State.Centre) : 0.f;
			Check(State, Broken && Distance > State.BrokenStartDistance + 300.f,
				FString::Printf(TEXT("runs back: %.0f -> %.0f cm from the squad (fallbacks %d)"), State.BrokenStartDistance, Distance, Tactics->GetFallBacks()));
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
		TEXT("CodexTactics.EnemyTacticsSmoke"),
		TEXT("Dev check: melee pack tactics — spread over the squad, flanking, morale fall-back; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
