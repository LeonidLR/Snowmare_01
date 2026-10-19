// Dev-only headless check of the Sprint 08 line of sight on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.SightSmoke -Log Smoke-Sight.log
// A wave fight with the wave removed; a 60 cm barricade 2 m in front of the crouched squad, a frozen marksman 1 m behind
// it. Checks: standing marksman seen; prone -> hidden + silhouette; the operative stands -> seen; crouches -> hidden;
// the squad 14 m back (out of earshot) -> the silhouette stays where it was while the marksman crawls along the cover;
// his shot demasks him for 2 s, then he hides again with the silhouette at the firing spot; symmetric: a standing
// enemy 11 m off does not perceive a prone operative 1 m behind the barricade, does once he crouches. Animation while
// hidden (user report 2026-10-06, T-pose silhouettes): the hidden marksman's mesh refreshes its bones every frame and gets
// its authored tick option back when seen; a heard silhouette ticks (follows + animates), an unheard one is frozen.
// First, EXPLORING (symmetric sight, game designer 2026-10-19: "prone behind a wall the enemies don't see us, yet we still see them"):
// a 1.2 m wall 1 m in front of the leader, a frozen frostbitten (senses off) 6 m beyond it, the mates parked far behind: prone ->
// the squad's sight is on in exploration, the enemy is hidden from the squad and NOT targetable (no shot line, no automatic target,
// Ctrl + click refused - no fight), only its heard silhouette within 12 m; standing -> seen over the wall and targetable; crouched
// -> hidden again (95 cm eyes); out of earshot the last-known silhouette fades after 12 s (exploring only).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Debug/SmokeUtils.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/MarksmanEnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/EnemyGhostActor.h"
#include "Combat/HealthComponent.h"
#include "Combat/SightRules.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Combat/WaveSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/BarricadeActor.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"

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
		EVisibilityBasedAnimTickOption AuthoredTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPose;
		/** The exploration part (2026-10-19): the 1.2 m wall and the frostbitten behind it. */
		TWeakObjectPtr<AStaticMeshActor> Wall;
		TWeakObjectPtr<AEnemyCharacter> Lurker;
	};

	AStaticMeshActor* SightSmokeSpawnWall(UWorld* World, const FVector& Centre, const FRotator& Rotation, const FVector& Scale)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		AStaticMeshActor* Block = Cube ? World->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FTransform(Rotation, Centre, Scale),
			nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn) : nullptr;
		if (!Block)
		{
			return nullptr;
		}
		Block->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Block->GetStaticMeshComponent()->SetStaticMesh(Cube);
		Block->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
		Block->FinishSpawning(FTransform(Rotation, Centre, Scale));
		return Block;
	}

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
	const auto TickOption = [&]() { return Marksman && Marksman->GetMesh() ? Marksman->GetMesh()->VisibilityBasedAnimTickOption : State.AuthoredTickOption; };
		AEnemyCharacter* Lurker = State.Lurker.Get();
		const auto LurkerHidden = [&]() { return Lurker && !Sight->IsVisibleToSquad(Lurker); };
		switch (State.Stage)
		{
		case 100:
		{
			if (State.Time < 3.f)
			{
				return true;
			}
			// EXPLORING (symmetric sight 2026-10-19): a 1.2 m wall, a frostbitten 6 m beyond it, the leader prone 1 m behind it.
			Check(State, Flow->GetPhase() == ECodexGamePhase::Exploration && Sight->IsSquadSightActive() && !Sight->IsActive(),
				TEXT("exploring: the squad's sight runs (the fight rules do not)"));
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->Destroy();
			}
			State.Op = Squad->GetLeader();
			Op = State.Op.Get();
			State.F = Op->GetActorForwardVector().GetSafeNormal2D();
			State.R = FVector::CrossProduct(FVector::UpVector, State.F);
			State.P = Op->GetActorLocation();
			State.GroundZ = Op->GetActorLocation().Z - Op->GetSimpleCollisionHalfHeight();
			const FVector WallSpot = State.P + State.F * 100.f;
			State.Wall = SightSmokeSpawnWall(World, FVector(WallSpot.X, WallSpot.Y, State.GroundZ + 60.f), State.F.Rotation(), FVector(0.3f, 5.f, 1.2f));
			const FVector LurkerSpot = State.P + State.F * 700.f;
			State.Lurker = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::Frostbitten, LurkerSpot + FVector(0.f, 0.f, 100.f), (-State.F).Rotation());
			Lurker = State.Lurker.Get();
			if (!Lurker || !State.Wall.IsValid())
			{
				Check(State, false, TEXT("wall and frostbitten spawned"));
				return Finish(State);
			}
			Lurker->bOverridePerception = true;
			Lurker->PerceptionOverride.SightRangeCm = 0.f;
			Lurker->PerceptionOverride.ProximityCm = 0.f;
			Lurker->PerceptionOverride.HearWalkCm = Lurker->PerceptionOverride.HearRunCm = 0.f;
			Lurker->PerceptionOverride.HearCrouchWalkCm = Lurker->PerceptionOverride.HearCrawlCm = 0.f;
			Lurker->PerceptionOverride.SmellRadiusCm = 0.f;
			Lurker->CustomTimeDilation = 0.f;
			Lurker->SetActorTickEnabled(false);
			Lurker->SetActorLocation(FVector(LurkerSpot.X, LurkerSpot.Y, State.GroundZ + Lurker->GetSimpleCollisionHalfHeight() + 2.f));
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->StopOperative();
				if (Member != Op)
				{
					// The mates far behind (40 m): only the leader's eyes count here.
					const FVector Back = State.P - State.F * 4000.f + State.R * (Member == Squad->GetMembers().Last() ? 200.f : -200.f);
					Member->TeleportTo(FVector(Back.X, Back.Y, State.GroundZ + Member->GetSimpleCollisionHalfHeight() + 5.f), State.F.Rotation(), false, true);
				}
				Member->SetStance(EOperativeStance::Prone);
			}
			State.Stage = 101;
			State.Time = 0.f;
			return true;
		}
		case 101:
		{
			if (State.Time < 1.f)
			{
				return true;
			}
			Sight->Refresh();
			Check(State, LurkerHidden() && Lurker->IsHidden(), FString::Printf(TEXT("prone 1 m behind the 1.2 m wall (eyes %.0f cm): the frostbitten 6 m beyond it is hidden from the squad"),
				SightRules::EyeHeight(EOperativeStance::Prone)));
			Check(State, !Op->CanHitEnemy(Lurker) && Op->FindBestCombatTarget() == nullptr, TEXT("... and not targetable (no shot line, no automatic target)"));
			if (ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(World->GetFirstPlayerController()))
			{
				Squad->SetLeader(Op);
				PC->IssueTargetedShot(Lurker); // Ctrl + click on it
			}
			Check(State, Op->GetManualPriorityTarget() == nullptr && Flow->GetPhase() == ECodexGamePhase::Exploration,
				TEXT("Ctrl + click on the unseen enemy: refused ('Can't see it from here'), no fight"));
			Check(State, Sight->GetGhost(Lurker) && Sight->GetGhost(Lurker)->IsHeard(), TEXT("6 m away it is heard: only a faint silhouette follows the sound"));
			Op->SetStance(EOperativeStance::Standing);
			State.Stage = 102;
			State.Time = 0.f;
			return true;
		}
		case 102:
		{
			if (State.Time < 1.f)
			{
				return true;
			}
			Sight->Refresh();
			Check(State, !LurkerHidden() && !Lurker->IsHidden() && !Sight->GetGhost(Lurker), TEXT("standing (eyes 160 cm): seen over the wall, no silhouette"));
			Check(State, Op->CanHitEnemy(Lurker), TEXT("... and targetable"));
			Op->SetStance(EOperativeStance::Crouching);
			State.Stage = 103;
			State.Time = 0.f;
			return true;
		}
		case 103:
		{
			if (State.Time < 1.f)
			{
				return true;
			}
			Sight->Refresh();
			Check(State, LurkerHidden() && Sight->GetGhost(Lurker), TEXT("crouched (eyes 95 cm) behind the 1.2 m wall: hidden again, a last-known silhouette"));
			// Out of earshot (14 m): the silhouette stays frozen, then fades like the minimap's last-known icon (exploring only).
			const FVector Far = State.P + State.F * 1500.f;
			Lurker->SetActorLocation(FVector(Far.X, Far.Y, State.GroundZ + Lurker->GetSimpleCollisionHalfHeight() + 2.f));
			Op->SetStance(EOperativeStance::Prone);
			State.Stage = 104;
			State.Time = 0.f;
			return true;
		}
		case 104:
		{
			if (State.Time < 1.f)
			{
				return true;
			}
			Sight->Refresh();
			Check(State, LurkerHidden() && Sight->GetGhost(Lurker) && !Sight->GetGhost(Lurker)->IsHeard(), TEXT("14 m away, unheard: the last-known silhouette stays (frozen)"));
			State.Stage = 105;
			State.Time = 0.f;
			return true;
		}
		case 105:
		{
			if (Sight->GetGhost(Lurker) && State.Time < 15.f)
			{
				return true;
			}
			Check(State, LurkerHidden() && !Sight->GetGhost(Lurker) && State.Time >= SightRules::LastKnownFadeSeconds - 2.f,
				FString::Printf(TEXT("exploring: the last-known silhouette fades after ~%.0f s (gone after %.1f s; the minimap's fade)"), SightRules::LastKnownFadeSeconds, State.Time));
			if (AStaticMeshActor* Wall = State.Wall.Get())
			{
				Wall->Destroy();
			}
			Lurker->Destroy();
			// The Sprint 08 fight part from its start.
			State.Stage = 0;
			State.Time = 3.f;
			return true;
		}
		case 0:
		{
			if (State.Time < 3.f)
			{
				return true;
			}
			SmokeUtils::StartWaveFight(Flow); // sudden horde start, its free pause released
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
			State.AuthoredTickOption = TickOption();
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
			Check(State, TickOption() == EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones,
				TEXT("hidden: his mesh keeps evaluating its pose (no T-pose while unseen)"));
			Check(State, Ghost() && Ghost()->IsHeard() && Ghost()->IsActorTickEnabled(), TEXT("heard 3 m away: the silhouette follows and animates"));
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
			Check(State, TickOption() == State.AuthoredTickOption, TEXT("seen: his authored anim tick option is back"));
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
			Check(State, Ghost() && !Ghost()->IsActorTickEnabled(), TEXT("an unheard silhouette is frozen (no tick)"));
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
		State->Stage = 100; // the exploration part first (2026-10-19)
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), 0.1f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.SightSmoke"),
		TEXT("Dev check of the line of sight: exploring behind a 1.2 m wall (2026-10-19), then the Sprint 08 fight: 60 cm cover, prone / standing, silhouette, demask, symmetry; PASS / FAIL."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
