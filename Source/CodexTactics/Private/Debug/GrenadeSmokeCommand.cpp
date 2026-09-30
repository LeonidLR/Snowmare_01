// Dev-only console command for a headless hand-grenade check on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.GrenadeSmoke
// 1. G-equivalent aim for the commander: indicators drawn, a 20 m point is clamped to 12 m standing / 9 m crouching;
// 2. a throw at a frozen brute 6 m ahead with a fuel barrel next to it: one grenade spent, the grenade flies, lands and
// explodes after the fuse: the brute is hurt, the barrel burns; 3. in combat a second grenade is thrown and the squad
// enters turn-based combat at once: the grenade is refunded and the rifle is back in hands
// (Godot main.gd grenade aim / _throw_grenade_at / Gorky 17 refund, Scenes/weapons/grenade.gd).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/GrenadeActor.h"
#include "Combat/GrenadeSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Data/WeaponDataAsset.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/BarrelActor.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "TimerManager.h"

namespace GrenadeSmoke
{
	constexpr float StepSeconds = 0.25f;

	struct FState
	{
		int32 Stage = 0;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Failures = 0;
		TWeakObjectPtr<AEnemyCharacter> Brute;
		TWeakObjectPtr<ABarrelActor> Barrel;
		float BruteHealth = 0.f;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("GrenadeSmoke"));
		return false;
	}

	void Next(FState& State)
	{
		++State.Stage;
		State.StageTime = 0.f;
	}

	int32 GrenadesInWorld(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<AGrenadeActor> It(World); It; ++It)
		{
			Count += IsValid(*It) ? 1 : 0;
		}
		return Count;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 40.f)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke stopped in stage %d"), State.Stage);
			return Finish(State, false);
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = Squad->GetLeader();
		UGrenadeSubsystem* Grenades = World->GetSubsystem<UGrenadeSubsystem>();
		switch (State.Stage)
		{
		case 0:
		{
			if (State.Time < 3.f)
			{
				return true;
			}
			const FVector Forward = Leader->GetActorForwardVector().GetSafeNormal2D();
			State.Brute = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::Brute, Leader->GetActorLocation() + Forward * 600.f);
			State.Brute->CustomTimeDilation = 0.f; // stays on the spot
			State.BruteHealth = State.Brute->GetHealthComponent()->GetCurrentHealth();
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			const FVector Side = FVector::CrossProduct(Forward, FVector::UpVector);
			State.Barrel = World->SpawnActor<ABarrelActor>(Leader->GetActorLocation() + Forward * 600.f + Side * 200.f - FVector(0.f, 0.f, 20.f), FRotator::ZeroRotator, Params);

			Leader->SwitchToWeaponById(TEXT("grenade"));
			Check(State, Grenades->StartAim(Leader) && Grenades->IsAiming(), TEXT("aim started"));
			Check(State, Grenades->GetAimActor() && Grenades->GetAimActor()->GetSegmentCount() > 100, TEXT("range ring, blast ring and arc drawn"));
			Check(State, Grenades->GetAimActor() && Grenades->GetAimActor()->IsBlastZoneShown(), TEXT("blast zone fill + crosshair (M_AoeBlast) at the aim point"));
			const FGrenadeAimInfo Far = Grenades->GetAimInfo(Leader->GetActorLocation() + Forward * 2000.f);
			Check(State, Far.bValid && !Far.bInRange && FMath::IsNearlyEqual(FVector::Dist2D(Far.Target, Leader->GetActorLocation()), 1200.f, 1.f),
				TEXT("20 m point clamped to 12 m standing"));
			Leader->SetStance(EOperativeStance::Crouching);
			Check(State, FMath::IsNearlyEqual(Grenades->GetAimInfo(Leader->GetActorLocation()).MaxRange, 900.f), TEXT("crouching: 9 m"));
			Leader->SetStance(EOperativeStance::Standing);

			const int32 Before = Leader->GrenadesCount;
			AGrenadeActor* Grenade = Grenades->ThrowAtCursor(State.Brute->GetActorLocation());
			Check(State, Grenade && Leader->GrenadesCount == Before - 1 && !Grenades->IsAiming(), TEXT("thrown: one grenade spent, aim closed"));
			Check(State, Grenade && FVector::Dist2D(Grenade->GetLandingPoint(), State.Brute->GetActorLocation()) < 5.f, TEXT("lands on the brute"));
			Next(State);
			return true;
		}
		case 1:
			if (GrenadesInWorld(World) > 0)
			{
				return true;
			}
			Check(State, State.StageTime > 2.f, FString::Printf(TEXT("release + flight + fuse took %.1f s"), State.StageTime));
			Check(State, State.Brute.IsValid() && State.Brute->GetHealthComponent()->GetCurrentHealth() < State.BruteHealth,
				FString::Printf(TEXT("brute hurt: %.0f -> %.0f"), State.BruteHealth, State.Brute.IsValid() ? State.Brute->GetHealthComponent()->GetCurrentHealth() : -1.f));
			Check(State, State.Barrel.IsValid() && State.Barrel->IsBurning(), TEXT("the barrel in the blast burns"));
			Next(State);
			return true;
		case 2:
		{
			// Combat: throw again and switch to turn-based combat while the grenade is still in the hand.
			UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
			Flow->TriggerCombatZone();
			Flow->FinishCutscene();
			Flow->FinishPreparation();
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				if (*It != State.Brute.Get())
				{
					It->Destroy();
				}
			}
			Leader->SwitchToWeaponById(TEXT("grenade"));
			Grenades->StartAim(Leader);
			const int32 Before = Leader->GrenadesCount;
			Check(State, Grenades->ThrowAtCursor(State.Brute->GetActorLocation()) != nullptr && Leader->GrenadesCount == Before - 1, TEXT("second grenade thrown"));
			Check(State, Flow->RequestEnterTurnBased(true) == EGameFlowResult::Ok, TEXT("turn-based combat entered"));
			Check(State, Leader->GrenadesCount == Before && GrenadesInWorld(World) == 0, TEXT("the grenade in flight is refunded"));
			Check(State, Leader->CurrentWeapon && Leader->CurrentWeapon->WeaponId != TEXT("grenade"), TEXT("no grenade in hands in turn-based combat"));
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
		TEXT("CodexTactics.GrenadeSmoke"),
		TEXT("Dev check: grenade aim (range by stance), throw, fuse, blast (enemy hurt, barrel burns), refund on turn-based; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
