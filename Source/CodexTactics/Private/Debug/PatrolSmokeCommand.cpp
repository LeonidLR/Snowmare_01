// Dev-only headless check of the Sprint 11 outpost patrols on L_MovementTest (the map is not saved):
//   Scripts/smoke.ps1 -Command CodexTactics.PatrolSmoke -Log Smoke-Patrol.log
// Exploration (no wave); the level's enemies are removed and the level is made an ambush level (CodexTactics.CombatStart
// ambush). A three-point spline route (APatrolRouteActor, loop, 1 s pauses) is laid 12 m ahead of the squad; a marksman
// walks it, an escort frost hound follows him. Their perception is switched off (override) so nobody is spotted.
// Checks: the marksman reaches waypoints 1 and 2 at the patrol pace; the hound stays within the tether band (<= 6 m once
// caught up) and keeps patrolling; a simulated tripwire blast 25 m away leaves both on patrol, one 15 m away sends both
// SEARCHING (user amendment 2026-10-06: no Engage, no fight); with the search time cut to 4 s they give up and walk the
// route again; an open dialogue holds them (no detection though the hound's nose is set to 100 m, nobody moves); once it
// closes the hound smells the squad, the patrol engages and the ambush fight starts (WaveCombat / RealTime, wave 1 =
// the two of them) — no "Start combat".

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/PatrolRouteActor.h"
#include "AI/PatrolRouteRules.h"
#include "AI/WorldAIPauseSubsystem.h"
#include "Data/DialogueSequenceAsset.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFlow/LevelEncounterSubsystem.h"
#include "UI/DialogueSubsystem.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/MarksmanEnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Components/SplineComponent.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"

namespace PatrolSmoke
{
	struct FState
	{
		float Time = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		FVector Origin = FVector::ZeroVector;
		FVector F = FVector::ForwardVector;
		FVector R = FVector::RightVector;
		TWeakObjectPtr<AMarksmanEnemyCharacter> Marksman;
		TWeakObjectPtr<AEnemyCharacter> Hound;
		TWeakObjectPtr<APatrolRouteActor> Route;
		bool bReached1 = false;
		bool bReached2 = false;
		float MaxEscortDistance = 0.f;
		float MaxMarksmanSpeed = 0.f;
		float MaxHeldSpeed = 0.f;
		bool bSearchEnded = false;
	};

	/** Perception that never detects anything (the walk / search part must not be cut short). */
	FEnemyPerceptionParams Senseless()
	{
		FEnemyPerceptionParams Params;
		Params.SightRangeCm = 0.f;
		Params.ProximityCm = 0.f;
		Params.HearWalkCm = Params.HearRunCm = Params.HearCrouchWalkCm = Params.HearCrawlCm = 0.f;
		Params.HearGunshotCm = Params.HearExplosionCm = 0.f;
		Params.SmellRadiusCm = 0.f;
		return Params;
	}

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("PatrolSmoke"));
		return false;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += 0.1f;
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		for (AOperativeCharacter* Member : Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>())
		{
			Member->ColdLevel = 0.f;
			Member->bTacticalCeaseFire = true;
		}
		AMarksmanEnemyCharacter* Marksman = State.Marksman.Get();
		AEnemyCharacter* Hound = State.Hound.Get();
		switch (State.Stage)
		{
		case 0:
		{
			if (State.Time < 3.f)
			{
				return true;
			}
			AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
			if (!Leader)
			{
				Check(State, false, TEXT("squad leader present"));
				return Finish(State);
			}
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->Destroy();
			}
			State.F = Leader->GetActorForwardVector().GetSafeNormal2D();
			State.R = FVector::CrossProduct(FVector::UpVector, State.F);
			const FVector Feet = Leader->GetActorLocation() - FVector(0.f, 0.f, Leader->GetSimpleCollisionHalfHeight());
			// Waypoints on the navmesh (the user re-lays the map: SmokeUtils adapts the directions).
			const FVector A = SmokeUtils::ClearPoint(World, Feet, Feet + State.F * 1200.f);
			const FVector B = SmokeUtils::ClearPoint(World, A, A + State.R * 700.f);
			const FVector C = SmokeUtils::ClearPoint(World, B, B + State.F * 600.f);
			State.Origin = A;
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			APatrolRouteActor* Route = World->SpawnActor<APatrolRouteActor>(A, FRotator::ZeroRotator, Params);
			State.Route = Route;
			USplineComponent* Spline = Route ? Route->GetRouteSpline() : nullptr;
			if (!Spline)
			{
				Check(State, false, TEXT("route spawned"));
				return Finish(State);
			}
			Spline->ClearSplinePoints(false);
			for (const FVector& Point : { A, B, C })
			{
				Spline->AddSplinePoint(Point, ESplineCoordinateSpace::World, false);
			}
			Spline->UpdateSpline();
			Route->bIsLoop = true;
			Route->DefaultWaitTimeSeconds = 1.f;
			UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
			State.Marksman = Cast<AMarksmanEnemyCharacter>(Waves->SpawnEnemy(EEnemyArchetype::Marksman, A + FVector(0.f, 0.f, 100.f), State.R.Rotation()));
			State.Hound = Waves->SpawnEnemy(EEnemyArchetype::FrostHound, A - State.F * 300.f + FVector(0.f, 0.f, 80.f), State.R.Rotation());
			Marksman = State.Marksman.Get();
			Hound = State.Hound.Get();
			if (!Marksman || !Hound)
			{
				Check(State, false, TEXT("marksman and hound spawned"));
				return Finish(State);
			}
			// Nobody is spotted until the dialogue part (perception rules: CodexTactics.AI.Perception.*).
			Marksman->MarksmanConfig.DetectionRange = 100.f;
			for (AEnemyCharacter* Enemy : { static_cast<AEnemyCharacter*>(Marksman), Hound })
			{
				Enemy->bOverridePerception = true;
				Enemy->PerceptionOverride = Senseless();
			}
			ULevelEncounterSubsystem* Encounter = World->GetSubsystem<ULevelEncounterSubsystem>();
			Encounter->SetCombatStartOverride(ECombatStartMode::Ambush);
			// Stealth check: a Passive squad keeps quiet (an Aggressive one would open fire on the patrol it sees).
			if (USquadSubsystem* PostureSquad = World->GetSubsystem<USquadSubsystem>())
			{
				PostureSquad->SetSquadPosture(ESquadFirePosture::Passive);
			}
			Encounter->SetPatrolSearchSecondsOverride(4.f);
			Check(State, Encounter->IsAmbushCombatStart(), TEXT("ambush level (no \"Start combat\")"));
			Marksman->StartPatrol(Route, nullptr);
			Hound->StartPatrol(nullptr, Marksman);
			Check(State, Marksman->IsOnPatrol() && Marksman->GetAIState() == EMarksmanAIState::Patrol, TEXT("marksman patrols the spline route"));
			Check(State, Hound->IsOnPatrol() && Hound->GetEscortLeader() == Marksman, TEXT("hound escorts him"));
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: route A %s, B %s, C %s"), *A.ToCompactString(), *B.ToCompactString(), *C.ToCompactString());
			State.Stage = 1;
			State.Time = 0.f;
			return true;
		}
		case 1:
		{
			if (!Marksman || !Hound)
			{
				Check(State, false, TEXT("patrol alive"));
				return Finish(State);
			}
			// Walks the route.
			const int32 Index = Marksman->GetPatrolWaypointIndex();
			State.bReached1 |= Index >= 1;
			State.bReached2 |= Index == 2 || (State.bReached1 && Index == 0);
			State.MaxMarksmanSpeed = FMath::Max(State.MaxMarksmanSpeed, Marksman->GetVelocity().Size2D());
			if (State.Time > 6.f)
			{
				State.MaxEscortDistance = FMath::Max(State.MaxEscortDistance, FVector::Dist2D(Marksman->GetActorLocation(), Hound->GetActorLocation()));
			}
			if (!Marksman->IsOnPatrol() || !Hound->IsOnPatrol())
			{
				Check(State, false, TEXT("nobody alerted while unseen"));
				return Finish(State);
			}
			if ((State.bReached1 && State.bReached2 && State.Time > 12.f) || State.Time > 40.f)
			{
				Check(State, State.bReached1, TEXT("marksman reached waypoint 1"));
				Check(State, State.bReached2, TEXT("marksman went on to waypoint 2"));
				Check(State, State.MaxMarksmanSpeed > 50.f && State.MaxMarksmanSpeed <= Marksman->PatrolWalkSpeed + 30.f,
					FString::Printf(TEXT("patrol pace (max %.0f cm/s, patrol %.0f)"), State.MaxMarksmanSpeed, Marksman->PatrolWalkSpeed));
				Check(State, State.MaxEscortDistance <= 600.f,
					FString::Printf(TEXT("escort keeps the tether (max %.0f cm after catching up)"), State.MaxEscortDistance));
				// A blast 25 m off: not heard.
				AEnemyCharacter::AlertPatrolsNearTrap(World, Marksman->GetActorLocation() + State.R * 2500.f);
				Check(State, Marksman->IsOnPatrol() && Hound->IsOnPatrol() && !Marksman->IsSearching() && !Hound->IsSearching(), TEXT("tripwire at 25 m: both stay on patrol, no search"));
				State.Stage = 2;
				State.Time = 0.f;
			}
			return true;
		}
		case 2:
		{
			if (!Marksman || !Hound)
			{
				Check(State, false, TEXT("patrol alive"));
				return Finish(State);
			}
			if (State.Time < 0.5f)
			{
				return true;
			}
			// A blast 15 m off the marksman: he searches (no Engage), his escort hunts with him, the level stays in exploration.
			AEnemyCharacter::AlertPatrolsNearTrap(World, Marksman->GetActorLocation() + State.R * 1500.f);
			const UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
			Check(State, Marksman->IsSearching() && Marksman->IsOnPatrol() && Marksman->GetAIState() == EMarksmanAIState::Patrol,
				TEXT("tripwire at 15 m: marksman searches (no engage)"));
			Check(State, Hound->IsSearching() && Hound->IsOnPatrol(), TEXT("escort hound searches with him"));
			Check(State, Flow && Flow->GetPhase() == ECodexGamePhase::Exploration, TEXT("trap: no fight (still exploring)"));
			State.Stage = 3;
			State.Time = 0.f;
			State.MaxMarksmanSpeed = 0.f;
			return true;
		}
		case 3:
		{
			if (!Marksman || !Hound)
			{
				Check(State, false, TEXT("patrol alive"));
				return Finish(State);
			}
			const UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
			if (!State.bSearchEnded)
			{
				if (Marksman->IsSearching() && State.Time < 10.f)
				{
					return true;
				}
				State.bSearchEnded = true;
				Check(State, !Marksman->IsSearching() && State.Time >= 3.5f, FString::Printf(TEXT("search over after %.1f s (search time 4 s)"), State.Time));
				Check(State, Marksman->IsOnPatrol() && Hound->IsOnPatrol() && !Hound->IsSearching(), TEXT("both back on patrol duty"));
				Check(State, Flow && Flow->GetPhase() == ECodexGamePhase::Exploration, TEXT("still exploring after the search"));
				State.Time = 0.f;
				return true;
			}
			State.MaxMarksmanSpeed = FMath::Max(State.MaxMarksmanSpeed, Marksman->GetVelocity().Size2D());
			if (State.Time < 3.f)
			{
				return true;
			}
			Check(State, State.MaxMarksmanSpeed > 50.f, FString::Printf(TEXT("walks the route again (max %.0f cm/s)"), State.MaxMarksmanSpeed));
			// A dialogue opens: the world AI is held. The hound's nose now reaches the squad — it must not use it yet.
			UDialogueSubsystem* Dialogue = World->GetSubsystem<UDialogueSubsystem>();
			const UDialogueSequenceAsset* Intro = LoadObject<UDialogueSequenceAsset>(nullptr, TEXT("/Game/Data/Dialogues/DA_DialogueIntro.DA_DialogueIntro"));
			if (!Intro)
			{
				UDialogueSequenceAsset* Fallback = NewObject<UDialogueSequenceAsset>(GetTransientPackage());
				Fallback->Lines.AddDefaulted(2);
				Intro = Fallback;
			}
			Dialogue->StartDialogue(Intro);
			Hound->PerceptionOverride.SmellRadiusCm = 10000.f;
			Check(State, Dialogue->IsDialogueOpen() && UWorldAIPauseSubsystem::IsPausedIn(World), TEXT("dialogue open: world AI paused"));
			State.Stage = 4;
			State.Time = 0.f;
			State.MaxHeldSpeed = 0.f;
			return true;
		}
		case 4:
		{
			if (!Marksman || !Hound)
			{
				Check(State, false, TEXT("patrol alive"));
				return Finish(State);
			}
			if (State.Time > 0.6f)
			{
				State.MaxHeldSpeed = FMath::Max(State.MaxHeldSpeed, FMath::Max(Marksman->GetVelocity().Size2D(), Hound->GetVelocity().Size2D()));
			}
			if (State.Time < 3.f)
			{
				return true;
			}
			const UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
			Check(State, Hound->IsOnPatrol() && Marksman->IsOnPatrol(), TEXT("dialogue: nobody detects the squad (hound nose 100 m)"));
			Check(State, State.MaxHeldSpeed < 15.f, FString::Printf(TEXT("dialogue: the patrol stands still (max %.0f cm/s)"), State.MaxHeldSpeed));
			Check(State, Flow && Flow->GetPhase() == ECodexGamePhase::Exploration, TEXT("dialogue: no fight"));
			World->GetSubsystem<UDialogueSubsystem>()->SkipDialogue();
			Check(State, !UWorldAIPauseSubsystem::IsPausedIn(World), TEXT("dialogue closed: world AI live"));
			State.Stage = 5;
			State.Time = 0.f;
			return true;
		}
		default:
		{
			const UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
			const bool bFight = Flow && Flow->GetPhase() == ECodexGamePhase::WaveCombat;
			if (!bFight && State.Time < 4.f)
			{
				return true;
			}
			const ULevelEncounterSubsystem* Encounter = World->GetSubsystem<ULevelEncounterSubsystem>();
			const UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
			Check(State, Hound && !Hound->IsOnPatrol(), TEXT("after the dialogue the hound smells the squad and engages"));
			Check(State, Marksman && !Marksman->IsOnPatrol(), TEXT("its leader engages with it"));
			Check(State, bFight && Flow->GetCombatMode() == ECodexCombatMode::RealTime && Flow->IsAmbushFight(),
				FString::Printf(TEXT("detection started the real-time fight by ambush (phase %d)"), Flow ? static_cast<int32>(Flow->GetPhase()) : -1));
			Check(State, Encounter && Encounter->GetAmbushStarts() == 1 && Encounter->GetLastTrigger() == EAmbushTrigger::PatrolDetection,
				TEXT("one ambush start, by the patrol's detection"));
			Check(State, Waves && Waves->IsWaveActive() && Waves->GetAliveEnemyCount() == 2,
				FString::Printf(TEXT("the patrol is the wave (alive %d, nothing spawned)"), Waves ? Waves->GetAliveEnemyCount() : -1));
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
		TEXT("CodexTactics.PatrolSmoke"),
		TEXT("Dev check of the spline patrols: route walk, escort tether, trap search 25 m / 15 m + timeout, dialogue hold, detection starts the ambush fight; PASS / FAIL."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
