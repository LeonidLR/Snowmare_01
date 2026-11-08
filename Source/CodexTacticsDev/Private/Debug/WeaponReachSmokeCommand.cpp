// Dev-only check of bug #39 (user 2026-11-04: "operatives with an M16 kill very distant enemies like a sniper" since the 120 m sight).
//   powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands WeaponReachSmoke   (map + -LevelJson from test_map.json)
// On L_Gen_Depot_1 at clear noon the leader (M16: effective 28 m, max reach 60 m) stands on the longest clear ground line from the squad's
// start; a frozen frost hound (healed every frame) is put on that line 80 m, 50 m and 20 m out, the leader facing it:
//  1. 80 m: he sees it (CanOperativeSee) but never fires (beyond the max reach);
//  2. 50 m: he fires, the per-shot chance is low (the falloff: table x ~0.57);
//  3. 20 m: he fires, the per-shot chance is high (inside the effective range: the table unchanged), several times the 50 m one,
//     and more of his rounds hit than at 50 m.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/PmcEncounterSubsystem.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/CombatPressureSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/SightRules.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Data/WeaponDataAsset.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Environment/EnvironmentSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

namespace WeaponReachSmoke
{
	constexpr float WarmupSeconds = 3.f;
	constexpr float Timeout = 240.f;

	enum class EStage : uint8 { Setup, Range, Done };

	struct FRange
	{
		float DistanceCm;
		float Seconds;
	};
	const FRange Ranges[] = { { 8000.f, 10.f }, { 5000.f, 24.f }, { 2000.f, 16.f } };

	struct FState
	{
		EStage Stage = EStage::Setup;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Failures = 0;
		int32 RangeIndex = 0;
		TWeakObjectPtr<AOperativeCharacter> Leader;
		TWeakObjectPtr<AEnemyCharacter> Target;
		FVector Feet = FVector::ZeroVector;
		float Yaw = 0.f;
		FVector Spots[UE_ARRAY_COUNT(Ranges)];
		int32 Shots[UE_ARRAY_COUNT(Ranges)] = {};
		int32 Hits[UE_ARRAY_COUNT(Ranges)] = {};
		double ChanceSum[UE_ARRAY_COUNT(Ranges)] = {};
		bool bSeen[UE_ARRAY_COUNT(Ranges)] = {};
		FDelegateHandle FiredHandle;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	FVector FeetOf(const ACharacter& Character)
	{
		return Character.GetActorLocation() - FVector(0.f, 0.f, Character.GetSimpleCollisionHalfHeight());
	}

	FCollisionQueryParams NoPawns(UWorld* World)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(WeaponReachSmoke), false);
		for (TActorIterator<APawn> It(World); It; ++It)
		{
			Params.AddIgnoredActor(*It);
		}
		return Params;
	}

	/** The ground under a planar point at about Z (within 60 cm; no roof / crate in the way). */
	bool GroundNear(UWorld* World, const FVector& Point, float Z, FVector& Out)
	{
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, FVector(Point.X, Point.Y, Z + 2500.f), FVector(Point.X, Point.Y, Z - 500.f), ECC_Visibility, NoPawns(World))
			|| FMath::Abs(Hit.ImpactPoint.Z - Z) > 60.f)
		{
			return false;
		}
		Out = Hit.ImpactPoint;
		return true;
	}

	bool Finish(UWorld* World, FState& State, bool bComplete)
	{
		if (AOperativeCharacter* Leader = State.Leader.Get())
		{
			Leader->OnWeaponFiredNative.Remove(State.FiredHandle);
		}
		if (AEnemyCharacter* Target = State.Target.Get())
		{
			Target->Destroy();
		}
		if (UEnvironmentSubsystem* Environment = UEnvironmentSubsystem::Get(World))
		{
			Environment->SetClockScale(1.f);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		State.Stage = EStage::Done;
		FPlatformMisc::RequestExit(false, TEXT("WeaponReachSmoke"));
		return false;
	}

	/** The leader on his spot facing the target; the target frozen on its spot at full health. */
	void Hold(FState& State)
	{
		AOperativeCharacter* Leader = State.Leader.Get();
		AEnemyCharacter* Target = State.Target.Get();
		if (Leader && FVector::Dist2D(FeetOf(*Leader), State.Feet) > 15.f)
		{
			Leader->StopOperative();
			Leader->SetActorLocation(State.Feet + FVector(0.f, 0.f, Leader->GetSimpleCollisionHalfHeight() + 2.f), false, nullptr, ETeleportType::TeleportPhysics);
		}
		if (Leader)
		{
			Leader->SetActorRotation(FRotator(0.f, State.Yaw, 0.f));
		}
		if (Target)
		{
			const FVector Spot = State.Spots[State.RangeIndex];
			if (FVector::Dist2D(FeetOf(*Target), Spot) > 10.f)
			{
				Target->SetActorLocation(Spot + FVector(0.f, 0.f, Target->GetSimpleCollisionHalfHeight() + 2.f), false, nullptr, ETeleportType::TeleportPhysics);
			}
			if (UHealthComponent* Health = Target->FindComponentByClass<UHealthComponent>())
			{
				Health->Heal(Health->GetMaxHealth());
			}
		}
	}

	bool Step(UWorld* World, FState& State, float Dt)
	{
		State.Time += Dt;
		State.StageTime += Dt;
		if (State.Time > Timeout)
		{
			Check(State, false, FString::Printf(TEXT("timeout in stage %d"), static_cast<int32>(State.Stage)));
			return Finish(World, State, false);
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UTacticalSightSubsystem* Sight = World->GetSubsystem<UTacticalSightSubsystem>();
		UEnvironmentSubsystem* Environment = UEnvironmentSubsystem::Get(World);
		if (!Squad || !Sight || !Squad->GetLeader() || !Environment || !Environment->IsInitialised())
		{
			Check(State, false, TEXT("squad with a leader, the sight and an initialised environment"));
			return Finish(World, State, false);
		}
		if (State.Stage != EStage::Setup)
		{
			if (!State.Leader.IsValid() || !State.Target.IsValid())
			{
				Check(State, false, FString::Printf(TEXT("the leader (%d) and the target (%d) still there"), State.Leader.IsValid() ? 1 : 0, State.Target.IsValid() ? 1 : 0));
				return Finish(World, State, false);
			}
			Hold(State);
		}

		switch (State.Stage)
		{
		case EStage::Setup:
		{
			if (UCombatPressureSubsystem* Pressure = World->GetSubsystem<UCombatPressureSubsystem>())
			{
				Pressure->SetAutoSpawn(false);
				Pressure->DespawnAll();
			}
			if (UPmcEncounterSubsystem* Pmc = UPmcEncounterSubsystem::Get(World))
			{
				Pmc->CancelPending();
			}
			if (IConsoleVariable* Immortal = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Cheat.Immortal")))
			{
				Immortal->Set(1, ECVF_SetByConsole);
			}
			Environment->SetClockScale(0.f);
			Environment->ForceWeather(EWeatherState::Clear, true);
			Environment->SetHour(12.f);
			AOperativeCharacter* Leader = Squad->GetLeader();
			State.Leader = Leader;
			Squad->EnterSoloMode();
			Leader->StopOperative();
			Leader->SetStance(EOperativeStance::Standing);
			State.Feet = FeetOf(*Leader);
			Check(State, Leader->CurrentWeapon && Leader->CurrentWeapon->WeaponId == TEXT("m16"), FString::Printf(TEXT("the leader carries the M16 (%s: effective %.0f m, max %.0f m)"),
				Leader->CurrentWeapon ? *Leader->CurrentWeapon->WeaponId : TEXT("none"), Leader->CurrentWeapon ? Leader->CurrentWeapon->GetEffectiveRangeCm() / 100.f : 0.f,
				Leader->CurrentWeapon ? Leader->CurrentWeapon->Stats.Range.MaxRangeCm / 100.f : 0.f));
			// A ground line from his eye clear to 80 m (the three spots on it).
			const FVector Eye = State.Feet + FVector(0.f, 0.f, SightRules::EyeHeight(EOperativeStance::Standing));
			bool bFound = false;
			for (float Yaw = 0.f; Yaw < 360.f && !bFound; Yaw += 5.f)
			{
				const FVector Dir = FRotator(0.f, Yaw, 0.f).Vector();
				bool bAll = true;
				for (int32 Index = 0; Index < UE_ARRAY_COUNT(Ranges) && bAll; ++Index)
				{
					FVector Ground;
					bAll = GroundNear(World, State.Feet + Dir * Ranges[Index].DistanceCm, static_cast<float>(State.Feet.Z), Ground)
						&& Sight->HasClearSight(Eye, Ground + FVector(0.f, 0.f, 60.f));
					State.Spots[Index] = Ground;
				}
				if (bAll)
				{
					bFound = true;
					State.Yaw = Yaw;
				}
			}
			Check(State, bFound, FString::Printf(TEXT("a clear ground line from the leader to 80 m (yaw %.0f)"), State.Yaw));
			if (!bFound)
			{
				return Finish(World, State, false);
			}
			AEnemyCharacter* Target = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::FrostHound,
				State.Spots[0] + FVector(0.f, 0.f, 60.f), FRotator(0.f, State.Yaw + 180.f, 0.f));
			Check(State, Target != nullptr, TEXT("a frost hound target spawned"));
			if (!Target)
			{
				return Finish(World, State, false);
			}
			Target->CustomTimeDilation = 0.f; // frozen: it neither runs nor bites
			if (UHealthComponent* Health = Target->FindComponentByClass<UHealthComponent>())
			{
				Health->SetMaxHealth(100000.f); // never killed by a lucky burst between two heals (the mates fire too)
			}
			State.Target = Target;
			FState* Raw = &State;
			State.FiredHandle = Leader->OnWeaponFiredNative.AddLambda([Raw](AOperativeCharacter* Shooter, AActor* Victim, bool bHit)
			{
				if (Raw->Stage == EStage::Range && Victim == Raw->Target.Get())
				{
					++Raw->Shots[Raw->RangeIndex];
					Raw->Hits[Raw->RangeIndex] += bHit ? 1 : 0;
					Raw->ChanceSum[Raw->RangeIndex] += Shooter ? Shooter->GetLastShotHitChance() : 0.0;
				}
			});
			State.RangeIndex = 0;
			State.Stage = EStage::Range;
			State.StageTime = 0.f;
			return true;
		}

		case EStage::Range:
		{
			const int32 Index = State.RangeIndex;
			AOperativeCharacter* Leader = State.Leader.Get();
			State.bSeen[Index] |= Sight->CanOperativeSee(*Leader, State.Target->GetActorLocation());
			if (State.StageTime < Ranges[Index].Seconds)
			{
				return true;
			}
			const float Metres = Ranges[Index].DistanceCm / 100.f;
			const double Chance = State.Shots[Index] > 0 ? State.ChanceSum[Index] / State.Shots[Index] : 0.0;
			UE_LOG(LogCodexTactics, Display, TEXT("[WeaponReach] %.0f m: seen %d, %d shots, %d hits, mean chance %.3f (falloff x%.3f)"), Metres, State.bSeen[Index] ? 1 : 0,
				State.Shots[Index], State.Hits[Index], Chance, Leader->GetRangeHitMultiplierAt(State.Target.Get(), Ranges[Index].DistanceCm));
			if (Index == 0)
			{
				Check(State, State.bSeen[Index] && State.Shots[Index] == 0, FString::Printf(TEXT("80 m: the target is seen (%d) but never fired at (%d shots; beyond the M16's 60 m)"),
					State.bSeen[Index] ? 1 : 0, State.Shots[Index]));
			}
			else if (Index == 1)
			{
				Check(State, State.Shots[Index] >= 5 && Chance > 0.f && Chance < 0.2, FString::Printf(TEXT("50 m: he fires (%d shots) at a low chance %.3f (< 0.2)"), State.Shots[Index], Chance));
			}
			else
			{
				const double Far = State.Shots[1] > 0 ? State.ChanceSum[1] / State.Shots[1] : 0.0;
				const double FarRate = State.Shots[1] > 0 ? static_cast<double>(State.Hits[1]) / State.Shots[1] : 0.0;
				const double NearRate = State.Shots[Index] > 0 ? static_cast<double>(State.Hits[Index]) / State.Shots[Index] : 0.0;
				Check(State, State.Shots[Index] >= 5 && Chance >= 0.35 && Chance >= 2.0 * Far,
					FString::Printf(TEXT("20 m: he fires (%d shots) at a high chance %.3f (>= 0.35 and >= 2x the 50 m %.3f)"), State.Shots[Index], Chance, Far));
				Check(State, NearRate > FarRate, FString::Printf(TEXT("more rounds hit at 20 m (%d / %d = %.2f) than at 50 m (%d / %d = %.2f)"), State.Hits[Index],
					State.Shots[Index], NearRate, State.Hits[1], State.Shots[1], FarRate));
			}
			if (++State.RangeIndex >= UE_ARRAY_COUNT(Ranges))
			{
				return Finish(World, State, true);
			}
			State.StageTime = 0.f;
			return true;
		}

		default:
			return false;
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TSharedRef<FState> State = MakeShared<FState>();
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakWorld, State]()
		{
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
			{
				UWorld* W = WeakWorld.Get();
				return W && State->Stage != EStage::Done && Step(W, *State, W->IsPaused() ? 0.f : W->GetDeltaSeconds());
			}), 0.f);
		}), WarmupSeconds, false);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.WeaponReachSmoke"),
		TEXT("Dev check (bug #39, L_Gen_Depot_1): an M16 operative never fires at a target 80 m out (seen, beyond the 60 m reach), fires at 50 m with a low ")
		TEXT("chance and at 20 m with a high one (effective range 28 m). PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
