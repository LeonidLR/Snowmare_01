// Dev-only headless check of the fire postures (user request 2026-10-06, FirePostureRules; semantics 2026-10-19) on L_MovementTest
// (not saved):
//   Scripts/smoke.ps1 -Command CodexTactics.PostureSmoke -Log Smoke-Posture.log
// The level's enemies are removed; the level is a «button» level (like the Demo 01 lake) and a frost hound (senses off), placed in
// the map (MarkLevelEnemy), patrols ~9 m ahead - contact with it starts the fight (fix 2026-10-19: Aggressive never fired there).
//   Exploration: Passive and Defensive squads keep quiet (no fight); "/" with the leader selected turns only him
//   Aggressive, Alt + "/" the whole squad -> they open fire on their own -> the fight starts (SquadAutoFire, the normal
//   encounter start path).
//   Fight (the hound frozen and tough): Passive -> no shot; Defensive -> fires at the hound, which hunts the squad (has detected
//   it), nobody provoked; the hound parked, an UNAWARE guard (placed, on its post, senses off) in sight: Defensive -> no shot until
//   the leader is attacked, then the leader fires back; a box-selected group gets its own posture (override) while the leader keeps
//   the squad's; the leader alone gets his own; Alt + key = squad-wide (overrides cleared);
//   Passive again -> the leader still obeys a Ctrl + click attack order (IssueTargetedShot).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/PatrolRouteActor.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Components/SplineComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFlow/LevelEncounterSubsystem.h"

namespace PostureSmoke
{
	constexpr float StepSeconds = 0.1f;

	struct FState
	{
		float Time = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		TWeakObjectPtr<AEnemyCharacter> Hound;
		/** The unaware guard of the Defensive check (stage 5 on). */
		TWeakObjectPtr<AEnemyCharacter> Guard;
		FVector GuardSpot = FVector::ZeroVector;
		/** Shots per operative since the last reset (OnWeaponFiredNative). */
		TMap<TWeakObjectPtr<AOperativeCharacter>, int32> Shots;
		bool bBound = false;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("PostureSmoke"));
		return false;
	}

	int32 TotalShots(const FState& State)
	{
		int32 Total = 0;
		for (const TPair<TWeakObjectPtr<AOperativeCharacter>, int32>& Entry : State.Shots)
		{
			Total += Entry.Value;
		}
		return Total;
	}

	void Next(FState& State, int32 Stage)
	{
		State.Stage = Stage;
		State.Time = 0.f;
		State.Shots.Reset();
	}

	/** Senses off: only the squad's own fire / the smoke may start anything. */
	void DeafAndBlind(AEnemyCharacter& Enemy)
	{
		Enemy.bOverridePerception = true;
		Enemy.PerceptionOverride.SightRangeCm = 0.f;
		Enemy.PerceptionOverride.ProximityCm = 0.f;
		Enemy.PerceptionOverride.HearWalkCm = Enemy.PerceptionOverride.HearRunCm = 0.f;
		Enemy.PerceptionOverride.HearCrouchWalkCm = Enemy.PerceptionOverride.HearCrawlCm = 0.f;
		Enemy.PerceptionOverride.HearGunshotCm = Enemy.PerceptionOverride.HearExplosionCm = 0.f;
		Enemy.PerceptionOverride.SmellRadiusCm = 0.f;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, TSharedRef<FState> StateRef)
	{
		FState& State = *StateRef;
		State.Time += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		ULevelEncounterSubsystem* Encounter = World->GetSubsystem<ULevelEncounterSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(World->GetFirstPlayerController());
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!Squad || !Flow || !Encounter || !PC || !Leader)
		{
			return State.Time < 5.f || Finish(State, false);
		}
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Member->ColdLevel = 0.f;
			Member->HealthComponent->SetMaxHealth(100000.f, false);
			Member->HealthComponent->Heal(100000.f);
		}
		if (!State.bBound)
		{
			State.bBound = true;
			TWeakPtr<FState> WeakState = StateRef;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->OnWeaponFiredNative.AddLambda([WeakState](AOperativeCharacter* Shooter, AActor*, bool)
				{
					if (TSharedPtr<FState> Pinned = WeakState.Pin())
					{
						++Pinned->Shots.FindOrAdd(Shooter);
					}
				});
			}
		}
		AEnemyCharacter* Hound = State.Hound.Get();
		if (Hound && Flow->GetPhase() == ECodexGamePhase::WaveCombat)
		{
			Hound->CustomTimeDilation = 0.f; // it never attacks; only the smoke's TakeHit does
			Hound->GetHealthComponent()->Heal(100000.f);
		}
		if (AEnemyCharacter* Guard = State.Guard.Get())
		{
			Guard->GetHealthComponent()->Heal(100000.f);
		}

		switch (State.Stage)
		{
		case 0:
		{
			if (State.Time < 3.f)
			{
				return true;
			}
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->Destroy();
			}
			// The Demo 01 lake case (fix 2026-10-19): a «button» level whose patrols are placed in the map.
			Encounter->SetCombatStartOverride(ECombatStartMode::Button);
			Squad->SetSquadPosture(ESquadFirePosture::Passive);
			const FVector F = Leader->GetActorForwardVector().GetSafeNormal2D();
			const FVector R = FVector::CrossProduct(FVector::UpVector, F);
			const FVector Feet = Leader->GetActorLocation() - FVector(0.f, 0.f, Leader->GetSimpleCollisionHalfHeight());
			const FVector A = SmokeUtils::ClearPoint(World, Feet, Feet + F * 900.f);
			const FVector B = SmokeUtils::ClearPoint(World, A, A + R * 400.f);
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			APatrolRouteActor* Route = World->SpawnActor<APatrolRouteActor>(A, FRotator::ZeroRotator, Params);
			USplineComponent* Spline = Route ? Route->GetRouteSpline() : nullptr;
			Hound = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::FrostHound, A + FVector(0.f, 0.f, 80.f), (-F).Rotation());
			if (!Spline || !Hound)
			{
				Check(State, false, TEXT("route and hound spawned"));
				return Finish(State, false);
			}
			Spline->ClearSplinePoints(false);
			Spline->AddSplinePoint(A, ESplineCoordinateSpace::World, false);
			Spline->AddSplinePoint(B, ESplineCoordinateSpace::World, false);
			Spline->UpdateSpline();
			Route->bPingPong = true;
			State.Hound = Hound;
			State.GuardSpot = A;
			Hound->GetHealthComponent()->SetMaxHealth(100000.f);
			DeafAndBlind(*Hound);
			Hound->StartPatrol(Route, nullptr);
			Encounter->MarkLevelEnemy(Hound); // placed in the map (it has a route: it keeps patrolling)
			Check(State, !Encounter->IsAmbushCombatStart() && Encounter->IsLevelEnemy(Hound) && Hound->IsOnPatrol(),
				TEXT("a «button» level, the patrolling hound is a level enemy (the lake setup)"));
			Check(State, Leader->CanHitEnemy(Hound) || Leader->FindBestCombatTarget() == Hound,
				TEXT("the hound is in the squad's sight and range"));
			Next(State, 1);
			return true;
		}
		case 1:
			if (State.Time < 2.5f)
			{
				return true;
			}
			Check(State, Flow->GetPhase() == ECodexGamePhase::Exploration && TotalShots(State) == 0, TEXT("Passive: no fire, no fight (stealth kept)"));
			Squad->SetSquadPosture(ESquadFirePosture::Defensive);
			Next(State, 2);
			return true;
		case 2:
			if (State.Time < 2.5f)
			{
				return true;
			}
			Check(State, Flow->GetPhase() == ECodexGamePhase::Exploration && TotalShots(State) == 0, TEXT("Defensive, not attacked: no fire, no fight"));
			Squad->SetSelectedGroup({ Leader }, false);
			// User decision 2026-10-06: "/" alone changes only the selected operative (the leader) ...
			PC->ApplyFirePosture(ESquadFirePosture::Aggressive);
			{
				bool bOthersKept = true;
				for (const AOperativeCharacter* Member : Squad->GetMembers())
				{
					bOthersKept &= Member == Leader || Squad->GetEffectivePosture(Member) == ESquadFirePosture::Defensive;
				}
				Check(State, Squad->GetEffectivePosture(Leader) == ESquadFirePosture::Aggressive && bOthersKept
					&& Squad->GetSquadPosture() == ESquadFirePosture::Defensive,
					TEXT("posture key, one operative selected: only the leader turns Aggressive"));
			}
			// ... and Alt + "/" the whole squad.
			PC->ApplyFirePosture(ESquadFirePosture::Aggressive, /*bSquadWide (Alt)*/ true);
			Check(State, Squad->GetSquadPosture() == ESquadFirePosture::Aggressive && Squad->CountPostureOverrides() == 0,
				TEXT("Alt + posture key: squad-wide Aggressive, overrides cleared"));
			Next(State, 3);
			return true;
		case 3:
			if (Flow->GetPhase() != ECodexGamePhase::WaveCombat && State.Time < 5.f)
			{
				return true;
			}
			Check(State, Flow->GetPhase() == ECodexGamePhase::WaveCombat && Flow->GetCombatMode() == ECodexCombatMode::RealTime,
				TEXT("Aggressive (button level, placed patrol): the squad opens fire on the patrol it sees -> the real-time fight"));
			Check(State, Encounter->GetAmbushStarts() == 1 && Encounter->GetLastTrigger() == EAmbushTrigger::SquadAutoFire,
				TEXT("started once, by the squad's own fire"));
			Squad->SetSquadPosture(ESquadFirePosture::Passive);
			Next(State, 4);
			return true;
		case 4:
			if (State.Time < 0.5f)
			{
				State.Shots.Reset(); // a shot already on its way when the posture changed
				return true;
			}
			if (State.Time < 3.f)
			{
				return true;
			}
			Check(State, TotalShots(State) == 0, FString::Printf(TEXT("fight, Passive: nobody fires on his own (%d shots)"), TotalShots(State)));
			Squad->SetSquadPosture(ESquadFirePosture::Defensive);
			// The (deaf and blind, frozen) hound DETECTS the squad: off its patrol, the leader its target - it hunts the squad.
			if (Hound)
			{
				if (Hound->IsOnPatrol())
				{
					Hound->BreakPatrol(EPatrolAlertCause::Sight, Leader->GetActorLocation());
				}
				Hound->SetCurrentTargetForTesting(Leader);
			}
			Next(State, 5);
			return true;
		case 5:
		{
			// User decision 2026-10-19: Defensive engages an enemy that has detected the squad (the hound hunts it now).
			const int32* LeaderShots = State.Shots.Find(Leader);
			const bool bHunting = Hound && Hound->IsHuntingSquad();
			if (TotalShots(State) == 0 && State.Time < 4.f)
			{
				return true;
			}
			Check(State, bHunting && TotalShots(State) > 0, FString::Printf(TEXT("fight, Defensive: the hound hunts the squad (detected it: %d) -> fired at (%d shots, leader %d)"),
				bHunting ? 1 : 0, TotalShots(State), LeaderShots ? *LeaderShots : 0));
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Check(State, !Member->bProvokedThisFight, FString::Printf(TEXT("%s not provoked (fired because the hound detected the squad)"), *Member->DisplayName.ToString()));
			}
			// An UNAWARE enemy now: the hound parked, a placed guard on its post (senses off, frozen) where the hound started.
			SmokeUtils::ParkEnemyFarAway(Hound);
			Hound->SetActorTickEnabled(false);
			AEnemyCharacter* Guard = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::Frostbitten,
				State.GuardSpot + FVector(0.f, 0.f, 100.f), (State.GuardSpot - Leader->GetActorLocation()).GetSafeNormal2D().Rotation());
			if (!Guard)
			{
				Check(State, false, TEXT("guard spawned"));
				return Finish(State, false);
			}
			State.Guard = Guard;
			Guard->GetHealthComponent()->SetMaxHealth(100000.f);
			DeafAndBlind(*Guard);
			Encounter->MarkLevelEnemy(Guard); // no route: it guards its post (on patrol duty, not hunting)
			Guard->CustomTimeDilation = 0.f;
			Guard->SetActorTickEnabled(false);
			Next(State, 51);
			return true;
		}
		case 51:
		{
			AEnemyCharacter* Guard = State.Guard.Get();
			if (State.Time < 0.5f)
			{
				State.Shots.Reset(); // shots already on their way at the hound
				return true;
			}
			if (State.Time < 3.f)
			{
				return true;
			}
			Check(State, Guard && Guard->IsGuardingPost() && !Guard->IsHuntingSquad() && Leader->CanHitEnemy(Guard),
				TEXT("the guard is unaware (on its post), in sight and range"));
			Check(State, TotalShots(State) == 0, FString::Printf(TEXT("fight, Defensive, an unaware enemy, not attacked: holds fire (%d shots)"), TotalShots(State)));
			Leader->TakeHit(1.f, TEXT("smoke"), false, true, Guard);
			Check(State, Leader->bProvokedThisFight && Squad->IsSquadProvoked(), TEXT("the attack provokes the leader"));
			Next(State, 6);
			return true;
		}
		case 6:
		{
			const int32* LeaderShots = State.Shots.Find(Leader);
			if ((!LeaderShots || *LeaderShots == 0) && State.Time < 4.f)
			{
				return true;
			}
			Check(State, LeaderShots && *LeaderShots > 0, TEXT("Defensive, attacked: the leader fires back (at the unaware guard too)"));
			// Per-operative override: a box-selected group gets its own posture.
			TArray<AOperativeCharacter*> Others = Squad->GetMembers();
			Others.Remove(Leader);
			Squad->SetSelectedGroup(Others, false);
			PC->ApplyFirePosture(ESquadFirePosture::Passive); // the "," key with a group selected
			bool bOthersPassive = Others.Num() > 0;
			for (const AOperativeCharacter* Other : Others)
			{
				bOthersPassive &= Squad->GetEffectivePosture(Other) == ESquadFirePosture::Passive;
			}
			Check(State, bOthersPassive && Squad->CountPostureOverrides() == Others.Num()
				&& Squad->GetEffectivePosture(Leader) == ESquadFirePosture::Defensive,
				TEXT("posture key with a group: only the selected get their own posture"));
			Squad->SetLeader(Leader); // drops the group
			PC->ApplyFirePosture(ESquadFirePosture::Aggressive); // "/" with only the leader selected
			Check(State, Squad->GetEffectivePosture(Leader) == ESquadFirePosture::Aggressive && bOthersPassive
				&& Squad->GetEffectivePosture(Others.IsEmpty() ? Leader : Others[0]) == ESquadFirePosture::Passive,
				TEXT("the leader alone gets his own posture, the group keeps theirs (per-operative)"));
			PC->ApplyFirePosture(ESquadFirePosture::Passive, /*bSquadWide (Alt)*/ true);
			Check(State, Squad->GetSquadPosture() == ESquadFirePosture::Passive && Squad->CountPostureOverrides() == 0,
				TEXT("Alt + posture key clears the overrides (squad-wide)"));
			Next(State, 7);
			return true;
		}
		case 7:
			if (State.Time < 0.5f)
			{
				State.Shots.Reset();
				return true;
			}
			if (State.Time < 2.f)
			{
				return true;
			}
			Check(State, TotalShots(State) == 0, TEXT("Passive though provoked: no fire of its own"));
			PC->IssueTargetedShot(State.Guard.Get()); // Ctrl + click: a direct order
			Next(State, 8);
			return true;
		default:
		{
			const int32* LeaderShots = State.Shots.Find(Leader);
			if ((!LeaderShots || *LeaderShots == 0) && State.Time < 4.f)
			{
				return true;
			}
			Check(State, LeaderShots && *LeaderShots > 0, TEXT("Passive obeys the attack order (Ctrl + click)"));
			Squad->SetSquadPosture(FirePostureRules::DefaultPosture);
			return Finish(State, true);
		}
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.PostureSmoke"),
		TEXT("Dev check of the fire postures (Passive / Defensive / Aggressive, overrides, ambush by auto-fire); PASS / FAIL."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
