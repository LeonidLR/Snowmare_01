// Dev-only check of the PMC squads as a level encounter (user request 2026-11-03, docs/systems/pmc-squad-ai.md 7) on the Depot:
//   Scripts/smoke.ps1 -Command CodexTactics.PmcEncounterSmoke   (map /Game/Maps/Generated/L_Gen_Depot_1, "-LevelJson=L_Gen_Depot_1.json")
// The level JSON's "pmc_squads" are scheduled (two squads, 30 s / 120 s); the first squad (defend) comes in 40..90 m from the squad on
// the navmesh, HIDDEN (fog of war: the meshes off, the actor present) and not seen; the squad is put in sight of it -> revealed; the
// fight starts (ambush skirmish), the player's men shoot the PMC and the PMC shoot back; when the PMC squad is down the skirmish ends
// and the game is back to exploration. The monster pressure is off and the later squads are dropped (one encounter at a time).
// CodexTactics.PmcEncounterHuntSmoke: the same with a HUNT squad brought in by the console command CodexTactics.PmcSpawn (50..90 m):
// it closes in on the squad (or makes contact) by itself, then the fight as above.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/PmcEncounterSubsystem.h"
#include "AI/PmcSquadSubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/CombatCheats.h"
#include "Combat/CombatPressureSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "NavigationSystem.h"

namespace PmcEncounterSmoke
{
	constexpr float StepSeconds = 0.1f;

	struct FState
	{
		int32 Stage = 0;
		double StageStart = -1.0;
		int32 Failures = 0;
		int32 TeamId = INDEX_NONE;
		TArray<TWeakObjectPtr<AOperativeCharacter>> Fighters;
		int32 SquadShots = 0;
		int32 SquadHits = 0;
		int32 PmcShots = 0;
		double LastSquadShot = 0.0;
		double LastHelp = 0.0;
		bool bFightSeen = false;
		int32 Helps = 0;
		bool bHunt = false;
		float StartDistanceCm = 0.f;
	};

	FVector Centre(const TArray<AOperativeCharacter*>& Men)
	{
		FVector Sum = FVector::ZeroVector;
		for (const AOperativeCharacter* Man : Men)
		{
			Sum += Man->GetActorLocation();
		}
		return Men.IsEmpty() ? Sum : Sum / Men.Num();
	}

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("PmcEncounterSmoke"));
		return false;
	}

	bool IsLive(const AOperativeCharacter* Operative)
	{
		return IsValid(Operative) && !Operative->IsKilledInAction() && Operative->HealthComponent && Operative->HealthComponent->IsAlive();
	}

	TArray<AOperativeCharacter*> LiveFighters(const FState& State)
	{
		TArray<AOperativeCharacter*> Out;
		for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Fighters)
		{
			if (IsLive(Weak.Get()))
			{
				Out.Add(Weak.Get());
			}
		}
		return Out;
	}

	/** Puts the squad 6-12 m from Target where a standing man's eyes have a clear line to him (same level), facing him. */
	bool BringSquadInSight(UWorld* World, const AOperativeCharacter& Target)
	{
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		const UTacticalSightSubsystem* Sight = World->GetSubsystem<UTacticalSightSubsystem>();
		if (!Squad || !Nav || !Sight)
		{
			return false;
		}
		const FVector Feet = Target.GetActorLocation() - FVector(0.f, 0.f, Target.GetSimpleCollisionHalfHeight());
		const FVector Profile = UTacticalSightSubsystem::ProfilePoint(Target);
		for (const float Distance : { 1000.f, 800.f, 600.f, 1200.f })
		{
			for (int32 Bearing = 0; Bearing < 12; ++Bearing)
			{
				const float Angle = Bearing * (2.f * PI / 12.f);
				const FVector Want = Feet + FVector(FMath::Cos(Angle) * Distance, FMath::Sin(Angle) * Distance, 0.f);
				FNavLocation OnNav;
				if (!Nav->ProjectPointToNavigation(Want, OnNav, FVector(150.f, 150.f, 200.f)) || FMath::Abs(OnNav.Location.Z - Feet.Z) > 150.f
					|| !Sight->HasClearSight(OnNav.Location + FVector(0.f, 0.f, 165.f), Profile))
				{
					continue;
				}
				const FVector Towards = (Feet - OnNav.Location).GetSafeNormal2D();
				const FVector Right(-Towards.Y, Towards.X, 0.f);
				const TArray<AOperativeCharacter*> Members = Squad->GetMembers();
				for (int32 Index = 0; Index < Members.Num(); ++Index)
				{
					Members[Index]->StopOperative();
					const FVector Spot = SmokeUtils::FreeSpot(World, OnNav.Location + Right * ((Index - (Members.Num() - 1) * 0.5f) * 120.f)
						- Towards * ((Index % 2) * 100.f) + FVector(0.f, 0.f, 95.f), Members[Index]);
					Members[Index]->TeleportTo(Spot, Towards.Rotation(), false, true);
				}
				UE_LOG(LogCodexTactics, Display, TEXT("[PmcEncounterSmoke] squad put %.0f m from %s at %s"), Distance / 100.f, *Target.DisplayName.ToString(),
					*OnNav.Location.ToCompactString());
				return true;
			}
		}
		return false;
	}

	void Listen(TSharedRef<FState> State, AOperativeCharacter* Shooter, bool bSquad, UWorld* World)
	{
		TWeakObjectPtr<UWorld> WeakWorld(World);
		Shooter->OnWeaponFiredNative.AddLambda([State, bSquad, WeakWorld](AOperativeCharacter*, AActor* Target, bool bHit)
		{
			const AOperativeCharacter* Hit = Cast<AOperativeCharacter>(Target);
			if (bSquad && Hit && !Hit->IsPlayerTeam())
			{
				++State->SquadShots;
				State->SquadHits += bHit ? 1 : 0;
				State->LastSquadShot = WeakWorld.IsValid() ? WeakWorld->GetTimeSeconds() : 0.0;
			}
			else if (!bSquad && Hit && Hit->IsPlayerTeam())
			{
				++State->PmcShots;
			}
		});
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, TSharedRef<FState> StateRef)
	{
		FState& State = *StateRef;
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		const double Now = World->GetTimeSeconds();
		if (State.StageStart < 0.0)
		{
			State.StageStart = Now;
		}
		const double InStage = Now - State.StageStart;
		auto Next = [&State, Now](int32 Stage)
		{
			State.Stage = Stage;
			State.StageStart = Now;
		};
		if (Now > 400.0)
		{
			Check(State, false, FString::Printf(TEXT("finished within 400 s of game time (stage %d)"), State.Stage));
			return Finish(State, false);
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World);
		UPmcEncounterSubsystem* Encounters = UPmcEncounterSubsystem::Get(World);
		UTacticalSightSubsystem* Sight = World->GetSubsystem<UTacticalSightSubsystem>();
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		if (!Squad || !Pmc || !Encounters || !Sight || !Flow)
		{
			Check(State, false, TEXT("squad / PMC / encounter / sight / flow subsystems present"));
			return Finish(State, false);
		}
		State.bFightSeen |= Flow->GetPhase() == ECodexGamePhase::WaveCombat;
		switch (State.Stage)
		{
		case 0:
		{
			// The level data read, the squad on the map.
			if (InStage < 2.0 || (Encounters->GetPendingCount() == 0 && Encounters->GetSpawned().IsEmpty() && InStage < 10.0))
			{
				return true;
			}
			// The squad cannot die here (fix/pmc-hunt-honest-sight 2026-11-09): the smoke checks the encounter's flow, not the fight's balance - the
			// PMC sniper (seed 77) killed the player's commander, the mission failed and the smoke waited out its 900 s.
			if (IConsoleVariable* Immortal = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Cheat.Immortal")))
			{
				Immortal->Set(1, ECVF_SetByCode);
			}
			Check(State, CombatCheats::IsImmortal(World), TEXT("the squad is immortal for the smoke (Codex.Cheat.Immortal)"));
			if (UCombatPressureSubsystem* Pressure = World->GetSubsystem<UCombatPressureSubsystem>())
			{
				Pressure->SetAutoSpawn(false); // the Depot's monster pressure off: the PMC encounter alone
				Pressure->DespawnAll();
			}
			Check(State, Squad->GetMembers().Num() >= 3, FString::Printf(TEXT("the player's squad is on the map (%d men)"), Squad->GetMembers().Num()));
			Check(State, Encounters->GetPendingCount() == 2, FString::Printf(TEXT("the level JSON schedules two PMC squads (%d pending)"), Encounters->GetPendingCount()));
			Check(State, FMath::IsNearlyEqual(Encounters->GetPendingDueAt(0), 30.f) && FMath::IsNearlyEqual(Encounters->GetPendingDueAt(1), 120.f),
				FString::Printf(TEXT("... after 30 s and 120 s (%.0f, %.0f)"), Encounters->GetPendingDueAt(0), Encounters->GetPendingDueAt(1)));
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Listen(StateRef, Member, true, World);
			}
			if (State.bHunt)
			{
				Encounters->CancelPending();
				GEngine->Exec(World, TEXT("CodexTactics.PmcSpawn 3 pmc_commander.json hunt 50 90 seed=77"));
				Next(10);
				return true;
			}
			Next(1);
			return true;
		}
		case 10:
		{
			if (Encounters->GetSpawned().IsEmpty())
			{
				Check(State, false, TEXT("CodexTactics.PmcSpawn brought a squad in"));
				return Finish(State, false);
			}
			State.TeamId = Encounters->GetSpawned()[0].TeamId;
			for (AOperativeCharacter* Fighter : Pmc->GetTeamMembers(State.TeamId))
			{
				State.Fighters.Add(Fighter);
				Listen(StateRef, Fighter, false, World);
			}
			EPmcPosture Posture = EPmcPosture::Defend;
			Check(State, State.Fighters.Num() == 3 && Pmc->GetTeamPosture(State.TeamId, Posture) && Posture == EPmcPosture::Hunt,
				FString::Printf(TEXT("PmcSpawn: three hunters on team %d"), State.TeamId));
			bool bAllConcealed = true;
			for (AOperativeCharacter* Fighter : LiveFighters(State))
			{
				bAllConcealed &= Sight->IsHostileOperativeConcealed(Fighter);
			}
			Check(State, bAllConcealed, TEXT("they come in concealed"));
			State.StartDistanceCm = static_cast<float>(FVector::Dist2D(Centre(LiveFighters(State)), Centre(Squad->GetMembers())));
			Next(11);
			return true;
		}
		case 11:
		{
			const float Distance = static_cast<float>(FVector::Dist2D(Centre(LiveFighters(State)), Centre(Squad->GetMembers())));
			const bool bEngaged = Pmc->IsTeamEngaged(State.TeamId);
			if (!bEngaged && Distance > State.StartDistanceCm - 1500.f && InStage < 90.0)
			{
				return true;
			}
			Check(State, bEngaged || Distance <= State.StartDistanceCm - 1500.f, FString::Printf(TEXT("the hunters close in by themselves (%.0f -> %.0f m%s, %.0f s)"),
				State.StartDistanceCm / 100.f, Distance / 100.f, bEngaged ? TEXT(", contact") : TEXT(""), InStage));
			const TArray<AOperativeCharacter*> Live = LiveFighters(State);
			if (!bEngaged && (Live.IsEmpty() || !BringSquadInSight(World, *Live[0])))
			{
				Check(State, false, TEXT("a spot in sight of the hunters"));
				return Finish(State, false);
			}
			State.LastHelp = Now;
			Next(bEngaged ? 4 : 3);
			return true;
		}
		case 1:
		{
			if (Encounters->GetSpawned().IsEmpty())
			{
				if (Encounters->GetClock() > 45.f)
				{
					Check(State, false, TEXT("the first PMC squad came within 45 s"));
					return Finish(State, false);
				}
				return true;
			}
			Encounters->CancelPending(); // one encounter at a time
			const FPmcEncounterSpawn& First = Encounters->GetSpawned()[0];
			State.TeamId = First.TeamId;
			Check(State, FMath::Abs(Encounters->GetClock() - 30.f) < 1.f, FString::Printf(TEXT("the first squad came at %.1f s (30)"), Encounters->GetClock()));
			const float Distance = static_cast<float>(FVector::Dist2D(First.Spot, Centre(Squad->GetMembers()))) / 100.f;
			Check(State, Distance >= 39.f && Distance <= 91.f, FString::Printf(TEXT("its spot is 40..90 m from the squad (%.0f m, level %d)"), Distance, First.Level));
			for (AOperativeCharacter* Fighter : Pmc->GetTeamMembers(State.TeamId))
			{
				State.Fighters.Add(Fighter);
				Listen(StateRef, Fighter, false, World);
			}
			Check(State, State.Fighters.Num() == 3, FString::Printf(TEXT("three fighters on hostile team %d"), State.TeamId));
			EPmcPosture Posture = EPmcPosture::Hunt;
			Check(State, Pmc->GetTeamPosture(State.TeamId, Posture) && Posture == EPmcPosture::Defend, TEXT("posture defend (the JSON's first entry)"));
			Check(State, Pmc->IsTeamCommanded(State.TeamId), TEXT("the commander runs it (ambush)"));
			bool bAllConcealed = true;
			bool bAllPresent = true;
			bool bNoneVisible = true;
			for (AOperativeCharacter* Fighter : LiveFighters(State))
			{
				bAllConcealed &= Sight->IsHostileOperativeConcealed(Fighter) && Fighter->GetMesh() && Fighter->GetMesh()->bHiddenInGame;
				bAllPresent &= !Fighter->IsHidden();
				bNoneVisible &= !Sight->IsVisibleToSquad(Fighter);
			}
			Check(State, bNoneVisible, TEXT("the squad does not see them at the spawn"));
			Check(State, bAllConcealed, TEXT("they are concealed at once (fog of war: meshes hidden)"));
			Check(State, bAllPresent, TEXT("... while the actors stay present for the game systems (IsHidden false)"));
			Check(State, Flow->GetPhase() != ECodexGamePhase::WaveCombat, TEXT("still exploring"));
			Next(2);
			return true;
		}
		case 2:
		{
			if (InStage < 3.0)
			{
				return true;
			}
			bool bAllConcealed = true;
			for (AOperativeCharacter* Fighter : LiveFighters(State))
			{
				bAllConcealed &= Sight->IsHostileOperativeConcealed(Fighter);
			}
			Check(State, bAllConcealed, TEXT("still concealed 3 s later (unseen)"));
			Check(State, !Pmc->IsTeamEngaged(State.TeamId), TEXT("no contact yet"));
			const TArray<AOperativeCharacter*> Live = LiveFighters(State);
			if (Live.IsEmpty() || !BringSquadInSight(World, *Live[0]))
			{
				Check(State, false, TEXT("a spot in sight of the PMC squad for the player's men"));
				return Finish(State, false);
			}
			State.LastHelp = Now;
			Next(3);
			return true;
		}
		case 3:
		{
			const TArray<AOperativeCharacter*> Live = LiveFighters(State);
			bool bRevealed = false;
			for (AOperativeCharacter* Fighter : Live)
			{
				bRevealed |= !Sight->IsHostileOperativeConcealed(Fighter) && Sight->IsVisibleToSquad(Fighter) && Fighter->GetMesh() && !Fighter->GetMesh()->bHiddenInGame;
			}
			if (!bRevealed && InStage < 10.0)
			{
				return true;
			}
			Check(State, bRevealed, FString::Printf(TEXT("seen -> revealed (%.1f s)"), InStage));
			Next(4);
			return true;
		}
		case 4:
		{
			const TArray<AOperativeCharacter*> Live = LiveFighters(State);
			if (!Live.IsEmpty())
			{
				// A squad that has not fired for 15 s (the PMC fell back out of sight) is put in sight of the next fighter again.
				if (InStage > 150.0)
				{
					Check(State, false, FString::Printf(TEXT("the PMC squad is down within 150 s (%d alive; squad shots %d, hits %d; PMC shots %d)"),
						Live.Num(), State.SquadShots, State.SquadHits, State.PmcShots));
					return Finish(State, false);
				}
				if (Now - FMath::Max(State.LastSquadShot, State.LastHelp) > 15.0)
				{
					State.LastHelp = Now;
					State.Helps += BringSquadInSight(World, *Live[0]) ? 1 : 0;
				}
				return true;
			}
			Check(State, State.bFightSeen, TEXT("the contact started the fight (ambush skirmish)"));
			Check(State, State.SquadShots > 0 && State.SquadHits > 0, FString::Printf(TEXT("the player's men shot the PMC (%d shots, %d hits)"), State.SquadShots, State.SquadHits));
			Check(State, State.PmcShots > 0, FString::Printf(TEXT("the PMC shot back (%d shots)"), State.PmcShots));
			UE_LOG(LogCodexTactics, Display, TEXT("[PmcEncounterSmoke] PMC squad down in %.0f s (%d re-placements of the squad)"), InStage, State.Helps);
			Next(5);
			return true;
		}
		case 5:
		{
			const bool bBack = Flow->GetPhase() == ECodexGamePhase::Exploration;
			if (!bBack && InStage < 15.0)
			{
				return true;
			}
			Check(State, bBack, FString::Printf(TEXT("the skirmish ended: back to exploration (phase %s)"), *UEnum::GetValueAsString(Flow->GetPhase())));
			Check(State, !UPmcSquadSubsystem::HasLiveHostileOperatives(World), TEXT("no engaged hostile fighter left"));
			bool bShown = true;
			for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Fighters)
			{
				bShown &= !Weak.IsValid() || !Sight->IsHostileOperativeConcealed(Weak.Get());
			}
			Check(State, bShown, TEXT("the fallen are not concealed"));
			return Finish(State, true);
		}
		default:
			return Finish(State, false);
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World, bool bHunt)
	{
		if (!World)
		{
			return;
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		State->bHunt = bHunt;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.PmcEncounterSmoke"),
		TEXT("Dev check (Depot + L_Gen_Depot_1.json): the level JSON's PMC squad comes in hidden and unseen, is revealed when the squad sees it, ")
		TEXT("fights the squad (both sides fire), and its end returns the game to exploration; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { Run(Args, World, false); }));

	static FAutoConsoleCommandWithWorldAndArgs HuntCommand(
		TEXT("CodexTactics.PmcEncounterHuntSmoke"),
		TEXT("Dev check (Depot + L_Gen_Depot_1.json): CodexTactics.PmcSpawn brings a hunting PMC squad in concealed, it closes in on the squad by itself, ")
		TEXT("the fight, back to exploration; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { Run(Args, World, true); }));
}

#endif // !UE_BUILD_SHIPPING
