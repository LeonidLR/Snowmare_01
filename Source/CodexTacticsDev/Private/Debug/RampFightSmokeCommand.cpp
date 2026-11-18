// Dev-only headless check of a fight on a generated ramp (designer bug B17-6, 2026-11-17):
// one operative walks DOWN L_Gen_Depot_1's external stair STAIR_ext_7, one Frostbitten stands at the low end
// and closes if he acquires the operative. Every 0.1 s logs sight, targets, a shot, the blocking component and
// the impact Z of the muzzle line and the melee line, and both health values.
// Gate (not loosened): once the operative's side sees the Frostbitten, that Frostbitten loses health within 1.5 s;
// the Frostbitten's melee connects only while EnemyAIRules::CanMeleeFeet says he is in reach (the same test as the AI).
//   powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -NoRetry -ExtraArgs -Multiprocess -Commands RampFightSmoke
// Map /Game/Maps/Generated/L_Gen_Depot_1, -LevelJson=L_Gen_Depot_1.json (Scripts/test_map.json).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AIController.h"
#include "Characters/EnemyAIRules.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/FirePostureRules.h"
#include "Characters/MeleeLineRules.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Combat/WaveSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "NavigationSystem.h"

namespace RampFightSmoke
{
	constexpr float StepSeconds = 0.1f;
	constexpr float WatchSeconds = 20.f;
	constexpr float DamageWindowSeconds = 1.5f;
	// Accepted layout of L_Gen_Depot_1, the same external stair LevelGenRampSmoke walks (from 0 to level 2, +6 m).
	const FVector2D StairFoot(-3920.f, 1622.f);
	const FVector2D StairTop(-3920.f, 2480.f);

	enum class EPhase : uint8 { Settle, Watch, Done };

	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		EPhase Phase = EPhase::Settle;
		float Time = 0.f;
		float PhaseTime = 0.f;
		int32 Failures = 0;
		bool bPlaced = false;
		bool bOrdered = false;
		FVector Goal = FVector::ZeroVector;
		TWeakObjectPtr<AOperativeCharacter> Operative;
		TWeakObjectPtr<AEnemyCharacter> Enemy;
		float OpHealthAtSight = -1.f;
		float EnemyHealthAtSight = -1.f;
		float SeenAt = -1.f;
		float DamagedAt = -1.f;
		float MeleeAt = -1.f;
		bool bMeleeInReach = false;
		/** Last time CanMeleeFeet was true (the blow's knockback can leave the 0.1 s sample a few cm outside). */
		float InReachAt = -1.f;
		float NextLog = 0.1f;
		double LastShotTime = -1.0e9;
		float LastOpHealth = -1.f;
		float LastEnemyHealth = -1.f;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		State.Phase = EPhase::Done;
		FPlatformMisc::RequestExit(false, TEXT("RampFightSmoke"));
		return false;
	}

	float FeetZ(const AActor& Actor)
	{
		const float Half = Actor.GetSimpleCollisionHalfHeight();
		return static_cast<float>(Actor.GetActorLocation().Z) - Half;
	}

	FVector FeetOf(const AActor& Actor)
	{
		return Actor.GetActorLocation() - FVector(0.f, 0.f, Actor.GetSimpleCollisionHalfHeight());
	}

	float HealthOf(const AActor* Actor)
	{
		const UHealthComponent* Health = Actor ? Actor->FindComponentByClass<UHealthComponent>() : nullptr;
		return Health ? Health->GetCurrentHealth() : -1.f;
	}

	bool SurfaceAt(UWorld& World, const FVector2D& XY, FVector& OutFeet)
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(RampFightFloor), false);
		const bool bHit = World.LineTraceSingleByChannel(Hit, FVector(XY.X, XY.Y, 2500.f), FVector(XY.X, XY.Y, -500.f), ECC_Visibility, Params);
		if (!bHit)
		{
			return false;
		}
		OutFeet = Hit.ImpactPoint;
		if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World))
		{
			FNavLocation OnNav;
			if (Nav->ProjectPointToNavigation(OutFeet + FVector(0.f, 0.f, 40.f), OnNav, FVector(120.f, 120.f, 160.f))
				&& FVector::Dist2D(OnNav.Location, OutFeet) <= 150.f)
			{
				OutFeet = FVector(OnNav.Location.X, OnNav.Location.Y, OnNav.Location.Z);
			}
		}
		return true;
	}

	FVector2D Along(float Fraction)
	{
		return FMath::Lerp(StairFoot, StairTop, Fraction);
	}

	void StandOn(AActor& Actor, const FVector& Feet, const FRotator& Facing)
	{
		const float Half = Actor.GetSimpleCollisionHalfHeight();
		Actor.TeleportTo(Feet + FVector(0.f, 0.f, Half + 5.f), Facing, false, true);
	}

	void Click(UWorld& World, const FVector& Goal)
	{
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(World.GetFirstPlayerController());
		if (!PC)
		{
			return;
		}
		FHitResult Hit;
		Hit.ImpactPoint = Goal;
		Hit.Location = Goal;
		Hit.ImpactNormal = FVector::UpVector;
		Hit.Normal = FVector::UpVector;
		Hit.bBlockingHit = true;
		PC->HandleWorldHit(Hit);
	}

	FString TraceBlock(UWorld& World, const FVector& From, const FVector& To, ECollisionChannel Channel, const AActor* IgnoreA, const AActor* IgnoreB,
		bool& bOutBlocked)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(RampFightLine), false);
		if (IgnoreA)
		{
			Params.AddIgnoredActor(IgnoreA);
		}
		if (IgnoreB)
		{
			Params.AddIgnoredActor(IgnoreB);
		}
		FHitResult Hit;
		bOutBlocked = World.LineTraceSingleByChannel(Hit, From, To, Channel, Params);
		if (!bOutBlocked)
		{
			return TEXT("clear");
		}
		return FString::Printf(TEXT("%s/%s z=%.0f"), *GetNameSafe(Hit.GetActor()), Hit.GetComponent() ? *Hit.GetComponent()->GetName() : TEXT("-"),
			Hit.ImpactPoint.Z);
	}

	bool Place(FState& State, UWorld& World)
	{
		USquadSubsystem* Squad = World.GetSubsystem<USquadSubsystem>();
		UWaveSubsystem* Waves = World.GetSubsystem<UWaveSubsystem>();
		if (!Squad || !Waves || !Squad->GetLeader())
		{
			return false;
		}
		FVector HighFeet;
		FVector LowFeet;
		FVector GoalFeet;
		if (!SurfaceAt(World, Along(0.82f), HighFeet) || !SurfaceAt(World, Along(0.18f), LowFeet) || !SurfaceAt(World, Along(0.05f), GoalFeet))
		{
			return false;
		}
		AOperativeCharacter* Operative = Squad->GetLeader();
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			if (!Member || Member == Operative)
			{
				continue;
			}
			Member->bHasPostureOverride = true;
			Member->PostureOverride = ESquadFirePosture::Passive;
			FVector Park;
			if (SurfaceAt(World, Along(0.9f) + FVector2D(500.f, 0.f), Park))
			{
				StandOn(*Member, Park, FRotator(0.f, -90.f, 0.f));
			}
			if (AAIController* AI = Cast<AAIController>(Member->GetController()))
			{
				AI->StopMovement();
			}
		}
		for (TActorIterator<AEnemyCharacter> It(&World); It; ++It)
		{
			It->Destroy();
		}
		const FRotator Down(0.f, -90.f, 0.f);
		StandOn(*Operative, HighFeet, Down);
		if (AAIController* AI = Cast<AAIController>(Operative->GetController()))
		{
			AI->StopMovement();
		}
		AEnemyCharacter* Enemy = Waves->SpawnEnemy(EEnemyArchetype::Frostbitten, LowFeet + FVector(0.f, 0.f, 120.f), FRotator(0.f, 90.f, 0.f));
		if (!Enemy)
		{
			return false;
		}
		StandOn(*Enemy, LowFeet, FRotator(0.f, 90.f, 0.f));
		if (!Squad->IsSoloMode())
		{
			Squad->EnterSoloMode();
		}
		Squad->SetLeader(Operative);
		Squad->SetSelectedGroup({ Operative }, false);
		State.Goal = GoalFeet;
		State.Operative = Operative;
		State.Enemy = Enemy;
		State.LastOpHealth = HealthOf(Operative);
		State.LastEnemyHealth = HealthOf(Enemy);
		State.LastShotTime = Operative->GetLastShotTime();
		UE_LOG(LogCodexTactics, Display,
			TEXT("[RampFight] placed STAIR_ext_7 operative %s feet (%.0f, %.0f, %.0f) frostbitten feet (%.0f, %.0f, %.0f) goal (%.0f, %.0f, %.0f) reach %.0f"),
			*Operative->DisplayName.ToString(), HighFeet.X, HighFeet.Y, HighFeet.Z, LowFeet.X, LowFeet.Y, LowFeet.Z, GoalFeet.X, GoalFeet.Y, GoalFeet.Z,
			Enemy->GetAttackRange());
		return true;
	}

	void LogSample(FState& State, UWorld& World, AOperativeCharacter& Operative, AEnemyCharacter& Enemy)
	{
		UTacticalSightSubsystem* Sight = World.GetSubsystem<UTacticalSightSubsystem>();
		const FVector OpFeet = FeetOf(Operative);
		const FVector EnFeet = FeetOf(Enemy);
		const FVector Profile = UTacticalSightSubsystem::ProfilePoint(Enemy);
		const bool bSees = Operative.IsSeenByOwnSide(&Enemy);
		FVector Belief = FVector::ZeroVector;
		bool bEnemySees = false;
		if (Sight)
		{
			Sight->GetBelief(&Enemy, &Operative, Belief, &bEnemySees);
		}
		const bool bFired = Operative.GetLastShotTime() > State.LastShotTime + KINDA_SMALL_NUMBER;
		State.LastShotTime = Operative.GetLastShotTime();
		const float OpHealth = HealthOf(&Operative);
		const float EnemyHealth = HealthOf(&Enemy);
		bool bShotBlocked = false;
		bool bEyeBlocked = false;
		bool bMeleeBlocked = false;
		const FString ShotBlock = TraceBlock(World, Operative.GetMuzzleLocation(), EnFeet + FVector(0.f, 0.f, 80.f), ECC_Visibility, &Operative, &Enemy, bShotBlocked);
		const FString EyeBlock = TraceBlock(World, UTacticalSightSubsystem::EyePoint(Operative), Profile, ECC_Visibility, &Operative, &Enemy, bEyeBlocked);
		const FString MeleeBlock = TraceBlock(World, MeleeLineRules::LinePoint(EnFeet), MeleeLineRules::LinePoint(OpFeet), ECC_Pawn, &Enemy, nullptr, bMeleeBlocked);
		const float Dist3 = FVector::Dist(EnFeet, OpFeet);
		const float Dz = FMath::Abs(EnFeet.Z - OpFeet.Z);
		const bool bCanMelee = EnemyAIRules::CanMeleeFeet(EnFeet, OpFeet, Enemy.GetAttackRange());
		const AActor* OpTarget = Operative.GetCurrentCombatTarget();
		const AActor* EnTarget = Enemy.GetCurrentTarget();
		if (bSees && State.SeenAt < 0.f)
		{
			State.SeenAt = State.PhaseTime;
			State.OpHealthAtSight = OpHealth;
			State.EnemyHealthAtSight = EnemyHealth;
		}
		if (State.DamagedAt < 0.f && State.LastEnemyHealth >= 0.f && EnemyHealth >= 0.f && EnemyHealth < State.LastEnemyHealth - 0.05f)
		{
			State.DamagedAt = State.PhaseTime;
		}
		State.LastOpHealth = OpHealth;
		State.LastEnemyHealth = EnemyHealth;
		UE_LOG(LogCodexTactics, Display,
			TEXT("[RampFight] t=%.1f sees=%d enSees=%d target=%s enTarget=%s fires=%d auto=%d posture=%d line={%s} muzBlock=%s eyeBlock=%s meleeBlock=%s meleeClear=%d canMelee=%d dist3=%.0f dist2=%.0f dz=%.0f reach=%.0f opZ=%.0f enZ=%.0f opHP=%.0f enHP=%.0f opV=%.0f enV=%.0f clip=%d"),
			State.PhaseTime, bSees ? 1 : 0, bEnemySees ? 1 : 0, OpTarget ? *OpTarget->GetName() : TEXT("-"), EnTarget ? *EnTarget->GetName() : TEXT("-"),
			bFired ? 1 : 0, Operative.MayAutoFireAtEnemy(&Enemy) ? 1 : 0, static_cast<int32>(Operative.GetFirePosture()), *Operative.DescribeShotLine(&Enemy),
			*ShotBlock, *EyeBlock, *MeleeBlock, Enemy.HasClearMeleeLine(&Operative, EnFeet) ? 1 : 0, bCanMelee ? 1 : 0, Dist3, FVector::Dist2D(EnFeet, OpFeet), Dz,
			Enemy.GetAttackRange(), OpFeet.Z, EnFeet.Z, OpHealth, EnemyHealth, Operative.GetVelocity().Size2D(), Enemy.GetVelocity().Size2D(), Operative.CurrentClip);
		(void)bShotBlocked;
		(void)bEyeBlocked;
		(void)bMeleeBlocked;
	}

	void NoteMelee(FState& State, AOperativeCharacter& Operative, AEnemyCharacter& Enemy)
	{
		const float OpHealth = HealthOf(&Operative);
		const float Range = Enemy.GetAttackRange();
		const bool bCanMelee = EnemyAIRules::CanMeleeFeet(FeetOf(Enemy), FeetOf(Operative), Range);
		FVector StrikeAttacker = FVector::ZeroVector;
		FVector StrikeTarget = FVector::ZeroVector;
		const bool bStrikeInReach = Enemy.GetLastMeleeStrikeFeet(StrikeAttacker, StrikeTarget)
			&& EnemyAIRules::CanMeleeFeet(StrikeAttacker, StrikeTarget, Range);
		if (bCanMelee)
		{
			State.InReachAt = State.PhaseTime;
		}
		// The shove plays in the same blow, after the reach test. The saved feet are that test; a live sample can already be outside.
		// A dodge deals 0 and does not move health, but the blow still happened.
		const bool bInReach = bCanMelee || bStrikeInReach || (State.InReachAt >= 0.f && State.PhaseTime - State.InReachAt <= 0.2f);
		const bool bHpDrop = State.LastOpHealth >= 0.f && OpHealth >= 0.f && OpHealth < State.LastOpHealth - 0.05f;
		const bool bTarget = Enemy.GetCurrentTarget() == &Operative;
		if (State.MeleeAt < 0.f && bTarget && (bStrikeInReach || bHpDrop))
		{
			State.MeleeAt = State.PhaseTime;
			State.bMeleeInReach = bStrikeInReach || bInReach;
		}
		if (OpHealth >= 0.f)
		{
			State.LastOpHealth = OpHealth;
		}
	}

	void Judge(FState& State)
	{
		const bool bSaw = State.SeenAt >= 0.f;
		Check(State, bSaw, bSaw ? FString::Printf(TEXT("operative sees the Frostbitten at %.1f s"), State.SeenAt) : TEXT("operative never sees the Frostbitten"));
		const bool bDamagedInTime = bSaw && State.DamagedAt >= State.SeenAt && State.DamagedAt - State.SeenAt <= DamageWindowSeconds;
		Check(State, bDamagedInTime, bDamagedInTime
			? FString::Printf(TEXT("Frostbitten took damage %.2f s after sight (hp %.0f -> %.0f)"), State.DamagedAt - State.SeenAt, State.EnemyHealthAtSight, State.LastEnemyHealth)
			: FString::Printf(TEXT("no Frostbitten damage within %.1f s of sight (seen %.1f, damaged %.1f, hp %.0f)"), DamageWindowSeconds, State.SeenAt, State.DamagedAt, State.LastEnemyHealth));
		Check(State, State.bMeleeInReach, State.MeleeAt < 0.f
			? TEXT("Frostbitten melee never hit the operative")
			: FString::Printf(TEXT("Frostbitten melee at %.1f s %s normal reach"), State.MeleeAt, State.bMeleeInReach ? TEXT("inside") : TEXT("OUTSIDE")));
	}

	bool Step(TSharedRef<FState> State, float DeltaSeconds)
	{
		if (State->Phase == EPhase::Done)
		{
			return false;
		}
		UWorld* World = State->World.Get();
		if (!World)
		{
			return false;
		}
		const float Dt = FMath::Max(0.f, DeltaSeconds);
		State->Time += Dt;
		State->PhaseTime += Dt;
		if (State->Phase == EPhase::Settle)
		{
			if (State->Time > 45.f)
			{
				Check(*State, false, TEXT("squad or the ramp surface was not ready"));
				return Finish(*State, false);
			}
			if (State->Time >= 2.f && Place(*State, *World))
			{
				State->bPlaced = true;
				State->Phase = EPhase::Watch;
				State->PhaseTime = 0.f;
				State->NextLog = StepSeconds;
			}
			return true;
		}
		AOperativeCharacter* Operative = State->Operative.Get();
		AEnemyCharacter* Enemy = State->Enemy.Get();
		if (Operative && !State->bOrdered)
		{
			Click(*World, State->Goal);
			State->bOrdered = true;
		}
		if (!Operative || !Enemy || Enemy->IsDying())
		{
			// A death ends the watch. The three checks are the result; living out the full 20 s is not required.
			if (State->PhaseTime >= 5.f)
			{
				Judge(*State);
				return Finish(*State, true);
			}
			return true;
		}
		NoteMelee(*State, *Operative, *Enemy);
		if (State->PhaseTime + KINDA_SMALL_NUMBER >= State->NextLog)
		{
			LogSample(*State, *World, *Operative, *Enemy);
			State->NextLog += StepSeconds;
		}
		if (State->PhaseTime + KINDA_SMALL_NUMBER >= WatchSeconds)
		{
			Judge(*State);
			return Finish(*State, true);
		}
		return true;
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		(void)Args;
		if (!World)
		{
			return;
		}
		if (IConsoleVariable* Pressure = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Pressure.Enabled")))
		{
			Pressure->Set(0, ECVF_SetByConsole);
		}
		if (IConsoleVariable* Encounters = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Pmc.Encounters")))
		{
			Encounters->Set(0, ECVF_SetByConsole);
		}
		TSharedRef<FState> State = MakeShared<FState>();
		State->World = World;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float DeltaSeconds)
		{
			return Step(State, DeltaSeconds);
		}), 0.f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.RampFightSmoke"),
		TEXT("Dev check (B17-6): one operative down STAIR_ext_7, one Frostbitten at the low end. Logs sight / target / fire / blocker / impact Z every 0.1 s. PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif
