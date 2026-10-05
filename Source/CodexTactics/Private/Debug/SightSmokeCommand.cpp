// Dev-only headless check of the Sprint 08 line of sight on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.SightSmoke -Log Smoke-Sight.log
// A wave fight with the wave removed; a 60 cm barricade 2 m in front of the crouched squad, a frozen marksman 1 m behind
// it. Checks: standing marksman seen; prone -> hidden + silhouette; the operative stands -> seen; crouches -> hidden;
// the squad 14 m back (out of earshot) -> the silhouette stays where it was while the marksman crawls along the cover;
// his shot demasks him for 2 s, then he hides again with the silhouette at the firing spot; symmetric: a standing
// enemy 11 m off does not perceive a prone operative 1 m behind the barricade, does once he crouches.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/MarksmanEnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/EnemyGhostActor.h"
#include "Combat/HealthComponent.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/BarricadeActor.h"

namespace SightSmoke
{
	struct FState
	{
		float Time = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		FVector P = FVector::ZeroVector;
		FVector F = FVector::ForwardVector;
		FVector R = FVector::RightVector;
		float GroundZ = 0.f;
		TWeakObjectPtr<AOperativeCharacter> Op;
		TWeakObjectPtr<AMarksmanEnemyCharacter> Marksman;
		FVector GhostFeet = FVector::ZeroVector;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("SightSmoke"));
		return false;
	}

	void PlaceSquad(USquadSubsystem* Squad, const FState& State, const FVector& Centre)
	{
		int32 Index = 0;
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			const FVector Spot = Member == State.Op.Get() ? Centre : Centre - State.F * 150.f + State.R * (Index++ % 2 == 0 ? 150.f : -150.f);
			Member->StopOperative();
			Member->TeleportTo(FVector(Spot.X, Spot.Y, State.GroundZ + Member->GetSimpleCollisionHalfHeight() + 5.f), State.F.Rotation(), false, true);
			Member->SetStance(EOperativeStance::Crouching);
		}
	}

	void PlaceMarksman(const FState& State, const FVector& Spot, EOperativeStance Stance)
	{
		AMarksmanEnemyCharacter* Marksman = State.Marksman.Get();
		Marksman->SetMarksmanStance(Stance);
		Marksman->SetActorLocation(FVector(Spot.X, Spot.Y, State.GroundZ + Marksman->GetSimpleCollisionHalfHeight() + 2.f));
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += 0.1f;
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UTacticalSightSubsystem* Sight = World->GetSubsystem<UTacticalSightSubsystem>();
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Member->ColdLevel = 0.f;
			Member->bTacticalCeaseFire = true; // nobody shoots the frozen marksman
		}
		AMarksmanEnemyCharacter* Marksman = State.Marksman.Get();
		AOperativeCharacter* Op = State.Op.Get();
		const auto Hidden = [&]() { return Marksman && !Sight->IsVisibleToSquad(Marksman); };
		const auto Ghost = [&]() { return Marksman ? Sight->GetGhost(Marksman) : nullptr; };
		switch (State.Stage)
		{
		case 0:
		{
			if (State.Time < 3.f)
			{
				return true;
			}
			Flow->TriggerCombatZone();
			Flow->FinishCutscene();
			Flow->FinishPreparation();
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->Destroy();
			}
			State.Op = Squad->GetLeader();
			Op = State.Op.Get();
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->HealthComponent->SetMaxHealth(100000.f);
			}
			State.F = Op->GetActorForwardVector().GetSafeNormal2D();
			State.R = FVector::CrossProduct(FVector::UpVector, State.F);
			State.P = Op->GetActorLocation();
			State.GroundZ = Op->GetActorLocation().Z - Op->GetSimpleCollisionHalfHeight();
			const FVector BarricadeSpot = State.P + State.F * 200.f;
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			World->SpawnActor<ABarricadeActor>(FVector(BarricadeSpot.X, BarricadeSpot.Y, State.GroundZ + ABarricadeActor::HeightCm * 0.5f),
				FRotator(0.f, State.F.Rotation().Yaw + 90.f, 0.f), Params);
			const FVector MarksmanSpot = State.P + State.F * 300.f;
			State.Marksman = Cast<AMarksmanEnemyCharacter>(World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::Marksman,
				MarksmanSpot + FVector(0.f, 0.f, 100.f), (-State.F).Rotation()));
			Marksman = State.Marksman.Get();
			if (!Marksman)
			{
				Check(State, false, TEXT("marksman spawned"));
				return Finish(State);
			}
			Marksman->CustomTimeDilation = 0.f; // frozen: we place it
			Marksman->GetHealthComponent()->SetMaxHealth(100000.f);
			PlaceSquad(Squad, State, State.P);
			PlaceMarksman(State, MarksmanSpot, EOperativeStance::Standing);
			State.Stage = 1;
			State.Time = 0.f;
			return true;
		}
		case 1:
			if (State.Time < 1.f)
			{
				return true;
			}
			Sight->Refresh();
			Check(State, Sight->IsActive() && !Hidden(), TEXT("a standing marksman behind the 60 cm barricade is seen by the crouched squad"));
			PlaceMarksman(State, State.P + State.F * 300.f, EOperativeStance::Prone);
			State.Stage = 2;
			State.Time = 0.f;
			return true;
		case 2:
			if (State.Time < 0.5f)
			{
				return true;
			}
			Sight->Refresh();
			Check(State, Hidden() && Marksman->IsHidden() && Ghost(), TEXT("prone behind the barricade: hidden, silhouette at his spot"));
			Op->SetStance(EOperativeStance::Standing);
			State.Stage = 3;
			State.Time = 0.f;
			return true;
		case 3:
			if (State.Time < 1.f)
			{
				return true;
			}
			Sight->Refresh();
			Check(State, !Hidden() && !Ghost(), TEXT("the operative stands up 3 m away: the prone marksman is seen, no silhouette"));
			Op->SetStance(EOperativeStance::Crouching);
			State.Stage = 4;
			State.Time = 0.f;
			return true;
		case 4:
			if (State.Time < 1.f)
			{
				return true;
			}
			Sight->Refresh();
			Check(State, Hidden() && Ghost(), TEXT("crouched again: hidden"));
			PlaceSquad(Squad, State, State.P - State.F * 1100.f);
			State.Stage = 5;
			State.Time = 0.f;
			return true;
		case 5:
			if (State.Time < 1.f)
			{
				return true;
			}
			Sight->Refresh();
			Check(State, Hidden() && Ghost() && !Ghost()->IsHeard(), TEXT("squad 14 m back: still hidden, out of earshot"));
			State.GhostFeet = Ghost() ? Ghost()->GetLastKnownFeet() : FVector::ZeroVector;
			PlaceMarksman(State, State.P + State.F * 300.f + State.R * 120.f, EOperativeStance::Prone);
			State.Stage = 6;
			State.Time = 0.f;
			return true;
		case 6:
			if (State.Time < 0.5f)
			{
				return true;
			}
			Sight->Refresh();
			Check(State, Hidden() && Ghost() && FVector::Dist2D(Ghost()->GetLastKnownFeet(), State.GhostFeet) < 5.f,
				FString::Printf(TEXT("he crawls 1.2 m along the cover: the silhouette stays at the last known spot (%.0f cm off)"),
					Ghost() ? FVector::Dist2D(Ghost()->GetLastKnownFeet(), State.GhostFeet) : -1.f));
			Sight->NotifyFired(Marksman);
			Check(State, !Hidden() && !Marksman->IsHidden(), TEXT("his shot demasks him at once"));
			State.Stage = 7;
			State.Time = 0.f;
			return true;
		case 7:
			if (State.Time < 1.f)
			{
				return true;
			}
			Sight->Refresh();
			Check(State, !Hidden(), TEXT("still seen 1 s after the shot"));
			State.Stage = 8;
			State.Time = 0.f;
			return true;
		case 8:
			if (State.Time < 1.6f)
			{
				return true;
			}
			Sight->Refresh();
			Check(State, Hidden() && Ghost() && FVector::Dist2D(Ghost()->GetLastKnownFeet(), Marksman->GetActorLocation()) < 60.f,
				TEXT("after the 2 s demask: hidden again, silhouette at the firing spot"));
			// Symmetry: the marksman stands 10 m beyond the barricade, the operative lies 1 m behind it.
			PlaceSquad(Squad, State, State.P - State.F * 1600.f);
			Op->StopOperative();
			Op->TeleportTo(FVector(State.P.X, State.P.Y, State.GroundZ + Op->GetSimpleCollisionHalfHeight() + 5.f) + State.F * 100.f, State.F.Rotation(),
				false, true);
			Op->SetStance(EOperativeStance::Prone);
			PlaceMarksman(State, State.P + State.F * 1200.f, EOperativeStance::Standing);
			State.Stage = 9;
			State.Time = 0.f;
			return true;
		case 9:
		{
			if (State.Time < 1.5f)
			{
				return true;
			}
			Sight->Refresh();
			FVector Belief;
			bool bPerceived = true;
			const bool bKnows = Sight->GetBelief(Marksman, Op, Belief, &bPerceived);
			Check(State, !(bKnows && bPerceived), TEXT("a standing enemy 11 m off does not perceive the operative prone 1 m behind the barricade"));
			Op->SetStance(EOperativeStance::Crouching);
			State.Stage = 10;
			State.Time = 0.f;
			return true;
		}
		default:
		{
			if (State.Time < 1.f)
			{
				return true;
			}
			Sight->Refresh();
			FVector Belief;
			bool bPerceived = false;
			Check(State, Sight->GetBelief(Marksman, Op, Belief, &bPerceived) && bPerceived, TEXT("crouched, he is seen over the cover"));
			const FTacticalSightStats& Stats = Sight->GetStats();
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: hidden %d, revealed %d, ghosts %d (heard %d), demasks %d"), Stats.Hidden, Stats.Revealed,
				Stats.GhostsSpawned, Stats.HeardGhosts, Stats.Demasks);
			return Finish(State);
		}
		}
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
		TEXT("CodexTactics.SightSmoke"),
		TEXT("Dev check of the Sprint 08 line of sight: 60 cm cover, prone / standing, silhouette, demask, symmetry; PASS / FAIL."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
