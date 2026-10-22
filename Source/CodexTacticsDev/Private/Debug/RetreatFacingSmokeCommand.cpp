// Dev-only headless check of the user's request 2026-10-22 (ThreatFacingRules): "two operatives fire hard at enemies pushing them; I
// order them to retreat at a run - they turn and run, correct; but then they stand with their backs to the enemies running after
// them. After the run the operative must turn toward the enemy he was fighting." On L_MovementTest in a runtime arena:
//   Scripts/smoke_parallel.ps1 -Commands RetreatFacingSmoke
//  1. a real-time wave fight (the level's wave parked far away); two operatives, AGGRESSIVE, a pack of three frostbitten (tough)
//     14 m ahead walking at them: both open fire;
//  2. a sprint order 26 m back (away from the pack): they turn and run (backs to the pack while running), the pack falls behind out
//     of their sight (the squad's sight does not see behind their backs);
//  3. once they stand: both turn to face the pack (within 30 deg) and fire again;
//  4. an explicit facing order (SetFacingPoint) is kept: no pull back to the pack.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"

namespace RetreatFacingSmoke
{
	constexpr float StepSeconds = 0.05f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		FVector O = FVector::ZeroVector;
		FVector F = FVector::ForwardVector;
		FVector R = FVector::RightVector;
		TArray<TWeakObjectPtr<AOperativeCharacter>> Pair;
		TArray<TWeakObjectPtr<AEnemyCharacter>> Pack;
		TArray<FVector> Goals;
		TArray<double> ShotBefore;
		float MaxBackAngle = 0.f;
		bool bSawThreatTurn = false;
		float PackDistanceAtStop = 0.f;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("RetreatFacingSmoke"));
		return false;
	}

	void Next(FState& State)
	{
		++State.Stage;
		State.StageTime = 0.f;
	}

	/** Centre of the living pack. */
	bool PackCentre(const FState& State, FVector& OutCentre)
	{
		FVector Sum = FVector::ZeroVector;
		int32 Count = 0;
		for (const TWeakObjectPtr<AEnemyCharacter>& Enemy : State.Pack)
		{
			if (Enemy.IsValid() && !Enemy->IsDying())
			{
				Sum += Enemy->GetActorLocation();
				++Count;
			}
		}
		OutCentre = Count > 0 ? Sum / Count : FVector::ZeroVector;
		return Count > 0;
	}

	/** Angle between his facing and the direction to the nearest living pack member, deg. */
	float AngleToPack(const FState& State, const AOperativeCharacter& Operative)
	{
		float Best = 180.f;
		for (const TWeakObjectPtr<AEnemyCharacter>& Enemy : State.Pack)
		{
			if (Enemy.IsValid() && !Enemy->IsDying())
			{
				const FVector To = (Enemy->GetActorLocation() - Operative.GetActorLocation()).GetSafeNormal2D();
				const float Cos = FVector::DotProduct(To, Operative.GetActorForwardVector().GetSafeNormal2D());
				Best = FMath::Min(Best, FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Cos, -1.f, 1.f))));
			}
		}
		return Best;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 90.f)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke stopped in stage %d"), State.Stage);
			return World ? Finish(State, false) : false;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
		// The pack must keep coming (and live): tough frostbitten.
		for (const TWeakObjectPtr<AEnemyCharacter>& Enemy : State.Pack)
		{
			if (Enemy.IsValid() && Enemy->GetHealthComponent())
			{
				Enemy->GetHealthComponent()->Heal(100000.f);
			}
		}
		switch (State.Stage)
		{
		case 0:
		{
			if (State.StageTime < 3.f || !Squad || !Flow || !Waves || Squad->GetMembers().Num() < 3 || !Squad->GetLeader())
			{
				return State.Time < 20.f || Finish(State, false);
			}
			TArray<AOperativeCharacter*> Roster = Squad->GetMembers();
			Roster.Sort([](const AOperativeCharacter& A, const AOperativeCharacter& B) { return A.SquadIndex < B.SquadIndex; });
			TArray<const AActor*> Ignore;
			for (AOperativeCharacter* Member : Roster)
			{
				Ignore.Add(Member);
			}
			if (APawn* Pawn = UGameplayStatics::GetPlayerPawn(World, 0))
			{
				Ignore.Add(Pawn);
			}
			const FVector Feet = Roster[0]->GetActorLocation() - FVector(0.f, 0.f, Roster[0]->GetSimpleCollisionHalfHeight());
			const SmokeUtils::FClearArena Arena = SmokeUtils::FindClearArena(World, Feet, Roster[0]->GetActorForwardVector(),
				FBox2D(FVector2D(-3000.f, -700.f), FVector2D(1800.f, 700.f)), Ignore);
			if (!Arena.bFound)
			{
				Check(State, false, TEXT("an empty, flat 48 x 14 m spot for the smoke's own set-up"));
				return Finish(State, false);
			}
			State.O = Arena.Origin;
			State.F = Arena.Forward;
			State.R = Arena.Right;
			SmokeUtils::StartWaveFight(Flow);
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				SmokeUtils::ParkEnemyFarAway(*It);
				It->SetActorTickEnabled(false);
			}
			Squad->SetSquadPosture(ESquadFirePosture::Aggressive);
			Squad->SetFollowersHolding(true);
			for (int32 Index = 0; Index < Roster.Num(); ++Index)
			{
				AOperativeCharacter* Member = Roster[Index];
				Member->StopOperative();
				Member->ColdLevel = 0.f;
				Member->HealthComponent->SetMaxHealth(100000.f, false);
				Member->HealthComponent->Heal(100000.f);
				const FVector Spot = Index < 2 ? State.O + State.R * (Index == 0 ? -150.f : 150.f) : State.O - State.F * 600.f + State.R * 600.f;
				const FVector Free = SmokeUtils::FreeSpot(World, Spot, Member);
				Member->TeleportTo(FVector(Free.X, Free.Y, Free.Z + 4.f), State.F.Rotation(), false, true);
				if (Index < 2)
				{
					State.Pair.Add(Member);
				}
				else
				{
					Member->bTacticalCeaseFire = true; // the third one stays out of it
				}
			}
			for (int32 Index = 0; Index < 3; ++Index)
			{
				const FVector Spot = SmokeUtils::FreeSpot(World, State.O + State.F * 1400.f + State.R * ((Index - 1) * 250.f), nullptr);
				AEnemyCharacter* Enemy = Waves->SpawnEnemy(EEnemyArchetype::Frostbitten, Spot + FVector(0.f, 0.f, 10.f), (-State.F).Rotation());
				if (Enemy && Enemy->GetHealthComponent())
				{
					Enemy->GetHealthComponent()->SetMaxHealth(100000.f, false);
					Enemy->GetHealthComponent()->Heal(100000.f);
					State.Pack.Add(Enemy);
				}
			}
			Check(State, State.Pair.Num() == 2 && State.Pack.Num() == 3 && Flow->GetPhase() == ECodexGamePhase::WaveCombat
				&& Flow->GetCombatMode() == ECodexCombatMode::RealTime, TEXT("a real-time fight: two operatives, a pack of three 14 m ahead"));
			Next(State);
			return true;
		}
		case 1:
		{
			// Both fire at the pack.
			bool bBothFire = true;
			for (const TWeakObjectPtr<AOperativeCharacter>& Member : State.Pair)
			{
				bBothFire &= Member.IsValid() && Member->IsFacingCombatTarget() && World->GetTimeSeconds() - Member->GetLastShotTime() < 1.5;
			}
			if (!bBothFire && State.StageTime < 12.f)
			{
				return true;
			}
			Check(State, bBothFire, FString::Printf(TEXT("both fire at the pack (%.1f s)"), State.StageTime));
			// Retreat at a run, 16 m straight back.
			for (const TWeakObjectPtr<AOperativeCharacter>& Member : State.Pair)
			{
				const FVector Goal = SmokeUtils::FreeSpot(World, Member->GetActorLocation() - State.F * 2600.f, Member.Get());
				State.Goals.Add(Goal);
				Check(State, Member->OrderMoveTo(Goal, true) == EOperativeOrderResult::Accepted, FString::Printf(TEXT("%s: sprint 26 m back accepted"),
					*Member->DisplayName.ToString()));
			}
			Next(State);
			return true;
		}
		case 2:
		{
			bool bArrived = true;
			for (int32 Index = 0; Index < State.Pair.Num(); ++Index)
			{
				AOperativeCharacter* Member = State.Pair[Index].Get();
				bArrived &= Member && FVector::Dist2D(Member->GetActorLocation(), State.Goals[Index]) < 150.f && Member->GetVelocity().Size2D() < 10.f;
				if (Member && State.StageTime > 1.f && State.StageTime < 2.f)
				{
					State.MaxBackAngle = FMath::Max(State.MaxBackAngle, AngleToPack(State, *Member));
				}
			}
			if (!bArrived && State.StageTime < 15.f)
			{
				return true;
			}
			FVector Pack;
			if (State.Pair[0].IsValid() && PackCentre(State, Pack))
			{
				State.PackDistanceAtStop = FVector::Dist2D(Pack, State.Pair[0]->GetActorLocation());
			}
			Check(State, bArrived, FString::Printf(TEXT("both ran back and stopped (%.1f s), the pack %.1f m behind them"), State.StageTime,
				State.PackDistanceAtStop / 100.f));
			Check(State, State.MaxBackAngle > 120.f, FString::Printf(TEXT("running: they turned away from the pack (up to %.0f deg off it)"), State.MaxBackAngle));
			State.ShotBefore.Reset();
			for (const TWeakObjectPtr<AOperativeCharacter>& Member : State.Pair)
			{
				State.ShotBefore.Add(Member.IsValid() ? Member->GetLastShotTime() : 0.0);
			}
			Next(State);
			return true;
		}
		case 3:
		{
			for (const TWeakObjectPtr<AOperativeCharacter>& Member : State.Pair)
			{
				State.bSawThreatTurn |= Member.IsValid() && Member->IsTurningToThreat();
			}
			if (State.StageTime < 2.5f)
			{
				return true;
			}
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: the turn to the last threat %s"), State.bSawThreatTurn
				? TEXT("drove the facing (the pack was out of their sight)") : TEXT("was not needed (a target in their sights at once)"));
			for (int32 Index = 0; Index < State.Pair.Num(); ++Index)
			{
				const AOperativeCharacter* Member = State.Pair[Index].Get();
				const float Angle = Member ? AngleToPack(State, *Member) : 180.f;
				Check(State, Member && Angle <= 30.f, FString::Printf(TEXT("%s stands facing the pack again (%.0f deg off; turning to the threat %d, target in sights %d)"),
					Member ? *Member->DisplayName.ToString() : TEXT("-"), Angle, Member && Member->IsTurningToThreat() ? 1 : 0,
					Member && Member->IsFacingCombatTarget() ? 1 : 0));
			}
			Next(State);
			return true;
		}
		case 4:
		{
			bool bFiring = true;
			for (int32 Index = 0; Index < State.Pair.Num(); ++Index)
			{
				const AOperativeCharacter* Member = State.Pair[Index].Get();
				bFiring &= Member && Member->GetLastShotTime() > State.ShotBefore[Index];
			}
			if (!bFiring && State.StageTime < 10.f)
			{
				return true;
			}
			Check(State, bFiring, FString::Printf(TEXT("both fire at the pack again (%.1f s after facing it)"), State.StageTime));
			// 4. An explicit facing order is kept (no pull back to the pack): face away, the pack parked far.
			for (const TWeakObjectPtr<AEnemyCharacter>& Enemy : State.Pack)
			{
				if (Enemy.IsValid())
				{
					Enemy->SetActorLocation(State.O + State.F * 2400.f + FVector(0.f, 0.f, 100.f), false, nullptr, ETeleportType::TeleportPhysics);
					Enemy->CustomTimeDilation = 0.f;
				}
			}
			if (AOperativeCharacter* Member = State.Pair[0].Get())
			{
				Member->SetFacingPoint(Member->GetActorLocation() - State.F * 500.f);
			}
			Next(State);
			return true;
		}
		case 5:
		{
			if (State.StageTime < 2.f)
			{
				return true;
			}
			const AOperativeCharacter* Member = State.Pair[0].Get();
			const float Away = Member ? FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Member->GetActorForwardVector().GetSafeNormal2D(), -State.F), -1.f, 1.f))) : 180.f;
			const float ToPack = Member ? AngleToPack(State, *Member) : 0.f;
			Check(State, Member && !Member->IsTurningToThreat() && ToPack > 120.f,
				FString::Printf(TEXT("a facing order of the player is kept: no turn to the pack (%.0f deg off the ordered facing, %.0f deg off the pack, turning to the threat %d)"),
					Away, ToPack, Member && Member->IsTurningToThreat() ? 1 : 0));
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
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.RetreatFacingSmoke"),
		TEXT("Dev check (user request 2026-10-22): after a retreat at a run the operatives turn back to the enemies they fought; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
